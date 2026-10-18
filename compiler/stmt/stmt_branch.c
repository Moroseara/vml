#include "stmt.h"
#include "../expr/expr.h"

void c_skip_block_tokens(Compiler *c) {
    c_consume(c, TOK_LBRACE, "Expected '{'");
    int depth = 1;
    while (depth > 0 && c_check(c, TOK_EOF) == VM_FALSE) {
        if (c_match(c, TOK_LBRACE) == VM_TRUE) depth++;
        else if (c_match(c, TOK_RBRACE) == VM_TRUE) depth--;
        else c_advance(c);
    }
}

void c_compile_match_stmt(Compiler *c) {
    ExprResult target = c_compile_expr(c, VM_TYPE_I32);
    c_consume(c, TOK_LBRACE, "Expected '{' after match expression");

    vm_u32 end_jumps[32];
    int num_end_jumps = 0;

    while (c_check(c, TOK_RBRACE) == VM_FALSE && c_check(c, TOK_EOF) == VM_FALSE) {
        if (c_match(c, TOK_ELSE) == VM_TRUE) {
            c_match(c, TOK_FAT_ARROW);
            c_match(c, TOK_COLON);
            if (c_check(c, TOK_LBRACE) == VM_TRUE) c_compile_block(c);
            else c_compile_statement(c);
        } else {
            Lexer saved_lex = c->lex;
            Token saved_cur = c->cur;
            int destruct_ev = -1;
            Token bind_tok = {0};

            if (c_check(c, TOK_IDENT) == VM_TRUE) {
                Token etok = c->cur;
                c_advance(c);
                if (c_match(c, TOK_DOT) == VM_TRUE && c_check(c, TOK_IDENT) == VM_TRUE) {
                    Token vtok = c->cur;
                    c_advance(c);
                    char qname[64];
                    vm_str_join_dot(qname, 64, etok.start, etok.length, vtok.start, vtok.length);
                    int ev = c_resolve_enum_variant(c, qname, vm_str_len(qname));
                    if (ev >= 0 && c->enum_variants[ev].has_payload == VM_TRUE && c_match(c, TOK_LPAREN) == VM_TRUE) {
                        if (c_check(c, TOK_IDENT) == VM_TRUE) {
                            bind_tok = c->cur;
                            c_advance(c);
                            if (c_match(c, TOK_RPAREN) == VM_TRUE) {
                                destruct_ev = ev;
                            }
                        }
                    }
                }
            }

            vm_u8 pat_reg;
            if (destruct_ev >= 0) {
                pat_reg = c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, pat_reg, (vm_i16)c->enum_variants[destruct_ev].tag));
            } else {
                c->lex = saved_lex;
                c->cur = saved_cur;
                ExprResult pat = c_compile_expr(c, target.type);
                pat_reg = pat.reg;
            }

            vm_u8 cmp_r = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_EQ_I32, cmp_r, target.reg, pat_reg));
            vm_u32 next_arm_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_FALSE, cmp_r, 0));

            c_match(c, TOK_FAT_ARROW);
            c_match(c, TOK_COLON);

            if (destruct_ev >= 0) {
                c_begin_scope(c);
                VMDataType ptype = c->enum_variants[destruct_ev].payload_type;
                vm_u8 breg = c_declare_local(c, bind_tok, ptype, VM_FALSE, (VMReg){0});
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, breg, (vm_u8)(target.reg + 1), 0));
                if (c_check(c, TOK_LBRACE) == VM_TRUE) c_compile_block(c);
                else c_compile_statement(c);
                c_end_scope(c);
            } else {
                if (c_check(c, TOK_LBRACE) == VM_TRUE) c_compile_block(c);
                else c_compile_statement(c);
            }

            end_jumps[num_end_jumps++] = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, 0));
            vm_i16 arm_off = (vm_i16)(c->chunk->code_count - (next_arm_jmp + 1));
            c->chunk->code[next_arm_jmp] = VM_MAKE_ABx(OP_JMP_FALSE, cmp_r, arm_off);
        }
        c_match(c, TOK_COMMA);
    }
    c_consume(c, TOK_RBRACE, "Expected '}' after match arms");

    for (int i = 0; i < num_end_jumps; i++) {
        vm_u32 jidx = end_jumps[i];
        vm_i16 off = (vm_i16)(c->chunk->code_count - (jidx + 1));
        c->chunk->code[jidx] = VM_MAKE_ABx(OP_JMP, 0, off);
    }
}

void c_compile_if_stmt(Compiler *c) {
    if (c_match(c, TOK_CONST) == VM_TRUE) {
        vm_u32 saved_count = c->chunk->code_count;
        vm_u8 saved_reg = c->next_reg;
        ExprResult cond = c_compile_expr(c, VM_TYPE_BOOL);
        c->chunk->code_count = saved_count;
        c->next_reg = saved_reg;

        vm_bool truthy = (cond.type == VM_TYPE_BOOL) ? cond.const_val.b : vm_bool_from_int(cond.const_val.i32 != 0);
        if (truthy == VM_TRUE) {
            c_compile_block(c);
            if (c_match(c, TOK_ELSE) == VM_TRUE) c_skip_block_tokens(c);
        } else {
            c_skip_block_tokens(c);
            if (c_match(c, TOK_ELSE) == VM_TRUE) {
                if (c_match(c, TOK_IF) == VM_TRUE) c_compile_if_stmt(c);
                else c_compile_block(c);
            }
        }
        return;
    }

    if (c_match(c, TOK_LET) == VM_TRUE) {
        c_begin_scope(c);
        Token name = c->cur;
        c_consume(c, TOK_IDENT, "Expected variable name after 'if let'");
        VMDataType type = VM_TYPE_I32;
        vm_bool has_type = VM_FALSE;
        if (c_match(c, TOK_COLON) == VM_TRUE) { type = c_parse_type(c); has_type = VM_TRUE; }
        c_consume(c, TOK_ASSIGN, "Expected '=' in 'if let'");
        vm_u8 saved_reg = c->next_reg;
        ExprResult init = c_compile_expr(c, type);
        if (has_type == VM_FALSE) type = init.type;
        c->next_reg = saved_reg;
        vm_u8 var_reg = c_declare_local(c, name, type, VM_FALSE, init.const_val);
        if (init.reg != var_reg) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, var_reg, init.reg, 0));

        vm_u32 jmp_false_idx;
        if (c_match(c, TOK_SEMICOLON) == VM_TRUE) {
            ExprResult cond = c_compile_expr(c, VM_TYPE_BOOL);
            jmp_false_idx = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_FALSE, cond.reg, 0));
        } else {
            jmp_false_idx = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_NZ_I32, var_reg, 0));
        }
        c_compile_block(c);
        if (c_match(c, TOK_ELSE) == VM_TRUE) {
            vm_u32 jmp_end_idx = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, 0));
            vm_i16 false_off = (vm_i16)(c->chunk->code_count - (jmp_false_idx + 1));
            c->chunk->code[jmp_false_idx] = VM_MAKE_ABx(VM_GET_OP(c->chunk->code[jmp_false_idx]), VM_GET_A(c->chunk->code[jmp_false_idx]), false_off);
            if (c_match(c, TOK_IF) == VM_TRUE) c_compile_if_stmt(c);
            else c_compile_block(c);
            vm_i16 end_off = (vm_i16)(c->chunk->code_count - (jmp_end_idx + 1));
            c->chunk->code[jmp_end_idx] = VM_MAKE_ABx(OP_JMP, 0, end_off);
        } else {
            vm_i16 false_off = (vm_i16)(c->chunk->code_count - (jmp_false_idx + 1));
            c->chunk->code[jmp_false_idx] = VM_MAKE_ABx(VM_GET_OP(c->chunk->code[jmp_false_idx]), VM_GET_A(c->chunk->code[jmp_false_idx]), false_off);
        }
        c_end_scope(c);
        return;
    }

    ExprResult cond = c_compile_expr(c, VM_TYPE_BOOL);
    vm_u32 jmp_false_idx = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_FALSE, cond.reg, 0));
    c_compile_block(c);
    if (c_match(c, TOK_ELSE) == VM_TRUE) {
        vm_u32 jmp_end_idx = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, 0));
        vm_i16 false_off = (vm_i16)(c->chunk->code_count - (jmp_false_idx + 1));
        c->chunk->code[jmp_false_idx] = VM_MAKE_ABx(OP_JMP_FALSE, cond.reg, false_off);
        if (c_match(c, TOK_IF) == VM_TRUE) c_compile_if_stmt(c);
        else c_compile_block(c);
        vm_i16 end_off = (vm_i16)(c->chunk->code_count - (jmp_end_idx + 1));
        c->chunk->code[jmp_end_idx] = VM_MAKE_ABx(OP_JMP, 0, end_off);
    } else {
        vm_i16 false_off = (vm_i16)(c->chunk->code_count - (jmp_false_idx + 1));
        c->chunk->code[jmp_false_idx] = VM_MAKE_ABx(OP_JMP_FALSE, cond.reg, false_off);
    }
}
