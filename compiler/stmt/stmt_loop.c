#include "stmt.h"
#include "../expr/expr.h"

void c_compile_while_stmt(Compiler *c) {
    int prev_breaks = c->num_breaks;
    int prev_continues = c->num_continues;

    if (c_match(c, TOK_CONST) == VM_TRUE) {
        vm_u32 saved_count = c->chunk->code_count;
        vm_u8 saved_reg = c->next_reg;
        ExprResult cond = c_compile_expr(c, VM_TYPE_BOOL);
        c->chunk->code_count = saved_count;
        c->next_reg = saved_reg;
        vm_bool truthy = (cond.type == VM_TYPE_BOOL) ? cond.const_val.b : vm_bool_from_int(cond.const_val.i32 != 0);
        if (truthy == VM_FALSE) { c_skip_block_tokens(c); return; }

        vm_u32 loop_start = c->chunk->code_count;
        c_compile_block(c);
        while (c->num_continues > prev_continues) {
            vm_u32 c_idx = c->continue_patches[--c->num_continues];
            c->chunk->code[c_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)((vm_i32)loop_start - (vm_i32)(c_idx + 1)));
        }
        vm_i16 back_off = (vm_i16)((vm_i32)loop_start - (vm_i32)(c->chunk->code_count + 1));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, back_off));
        while (c->num_breaks > prev_breaks) {
            vm_u32 b_idx = c->break_patches[--c->num_breaks];
            c->chunk->code[b_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(c->chunk->code_count - (b_idx + 1)));
        }
        return;
    }

    if (c_match(c, TOK_LET) == VM_TRUE) {
        c_begin_scope(c);
        Token name = c->cur;
        c_consume(c, TOK_IDENT, "Expected variable name after 'while let'");
        VMDataType type = VM_TYPE_I32;
        if (c_match(c, TOK_COLON) == VM_TRUE) type = c_parse_type(c);
        c_consume(c, TOK_ASSIGN, "Expected '=' in 'while let'");
        vm_u8 var_reg = c_declare_local(c, name, type, VM_FALSE, (VMReg){0});
        vm_u32 loop_start = c->chunk->code_count;
        ExprResult init = c_compile_expr(c, type);
        if (init.reg != var_reg) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, var_reg, init.reg, 0));

        vm_u32 exit_jmp;
        if (c_match(c, TOK_SEMICOLON) == VM_TRUE) {
            ExprResult cond = c_compile_expr(c, VM_TYPE_BOOL);
            exit_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_FALSE, cond.reg, 0));
        } else {
            exit_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_NZ_I32, var_reg, 0));
        }
        c_compile_block(c);
        while (c->num_continues > prev_continues) {
            vm_u32 c_idx = c->continue_patches[--c->num_continues];
            c->chunk->code[c_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)((vm_i32)loop_start - (vm_i32)(c_idx + 1)));
        }
        vm_i16 back_off = (vm_i16)((vm_i32)loop_start - (vm_i32)(c->chunk->code_count + 1));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, back_off));
        c->chunk->code[exit_jmp] = VM_MAKE_ABx(VM_GET_OP(c->chunk->code[exit_jmp]), VM_GET_A(c->chunk->code[exit_jmp]), (vm_i16)(c->chunk->code_count - (exit_jmp + 1)));
        while (c->num_breaks > prev_breaks) {
            vm_u32 b_idx = c->break_patches[--c->num_breaks];
            c->chunk->code[b_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(c->chunk->code_count - (b_idx + 1)));
        }
        c_end_scope(c);
        return;
    }

    vm_u32 loop_start = c->chunk->code_count;
    ExprResult cond = c_compile_expr(c, VM_TYPE_BOOL);
    vm_u32 exit_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_FALSE, cond.reg, 0));
    c_compile_block(c);
    while (c->num_continues > prev_continues) {
        vm_u32 c_idx = c->continue_patches[--c->num_continues];
        c->chunk->code[c_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)((vm_i32)loop_start - (vm_i32)(c_idx + 1)));
    }
    vm_i16 back_off = (vm_i16)((vm_i32)loop_start - (vm_i32)(c->chunk->code_count + 1));
    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, back_off));
    c->chunk->code[exit_jmp] = VM_MAKE_ABx(OP_JMP_FALSE, cond.reg, (vm_i16)(c->chunk->code_count - (exit_jmp + 1)));
    while (c->num_breaks > prev_breaks) {
        vm_u32 b_idx = c->break_patches[--c->num_breaks];
        c->chunk->code[b_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(c->chunk->code_count - (b_idx + 1)));
    }
}

void c_compile_for_stmt(Compiler *c) {
    c_begin_scope(c);
    int prev_breaks = c->num_breaks;
    int prev_continues = c->num_continues;

    if (c_check(c, TOK_INT_LIT) == VM_TRUE) {
        Token dummy = {TOK_IDENT, "_iter", 5, c->cur.line, 0, 0.0};
        vm_u8 reg_i = c_declare_local(c, dummy, VM_TYPE_I32, VM_FALSE, (VMReg){0});
        vm_u8 reg_limit = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));
        ExprResult count_expr = c_compile_expr(c, VM_TYPE_I32);
        if (count_expr.reg != reg_limit) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_limit, count_expr.reg, 0));
        vm_u32 body_start = c->chunk->code_count;
        c_compile_block(c);
        vm_u32 step_ip = c->chunk->code_count;
        while (c->num_continues > prev_continues) {
            vm_u32 c_idx = c->continue_patches[--c->num_continues];
            c->chunk->code[c_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(step_ip - (c_idx + 1)));
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_FORLOOP_I32, reg_i, (vm_i16)((vm_i32)body_start - (vm_i32)(c->chunk->code_count + 1))));
        while (c->num_breaks > prev_breaks) {
            vm_u32 b_idx = c->break_patches[--c->num_breaks];
            c->chunk->code[b_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(c->chunk->code_count - (b_idx + 1)));
        }
        c_end_scope(c);
        return;
    }

    if (c_match(c, TOK_LPAREN) == VM_TRUE) {
        Token names[MAX_TUPLE_ELEMS];
        vm_u8 var_regs[MAX_TUPLE_ELEMS];
        vm_u8 num_vars = 0;
        do {
            names[num_vars] = c->cur;
            c_consume(c, TOK_IDENT, "Expected variable name");
            var_regs[num_vars] = c_declare_local(c, names[num_vars], VM_TYPE_I32, VM_FALSE, (VMReg){0});
            num_vars++;
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
        c_consume(c, TOK_RPAREN, "Expected ')'");
        c_consume(c, TOK_IN, "Expected 'in'");
        c_consume(c, TOK_LPAREN, "Expected '('");
        ExprResult items[16];
        int num_items = 0;
        do { items[num_items++] = c_compile_expr(c, VM_TYPE_I32); } while (c_match(c, TOK_COMMA) == VM_TRUE);
        c_consume(c, TOK_RPAREN, "Expected ')'");

        Lexer body_lex = c->lex;
        Token body_tok = c->cur;
        for (int it = 0; it < num_items; it++) {
            for (vm_u8 v = 0; v < num_vars; v++) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, var_regs[v], (vm_u8)(items[it].reg + v), 0));
            }
            c->lex = body_lex; c->cur = body_tok;
            c_compile_block(c);
        }
        c_end_scope(c);
        return;
    }

    c_match(c, TOK_LET);
    Token var1 = c->cur;
    c_consume(c, TOK_IDENT, "Expected loop variable");
    if (c_match(c, TOK_COLON) == VM_TRUE) (void)c_parse_type(c);

    vm_bool has_two_vars = VM_FALSE;
    Token var2 = var1;
    if (c_match(c, TOK_COMMA) == VM_TRUE) {
        has_two_vars = VM_TRUE;
        var2 = c->cur;
        c_consume(c, TOK_IDENT, "Expected second loop variable after ','");
        if (c_match(c, TOK_COLON) == VM_TRUE) (void)c_parse_type(c);
    }
    c_consume(c, TOK_IN, "Expected 'in'");

    Token idx_tok = has_two_vars ? var1 : (Token){TOK_IDENT, "_iter_idx", 9, c->cur.line, 0, 0.0};
    Token val_tok = has_two_vars ? var2 : var1;

    /* Reserve `reg_i` and `reg_limit` first so integer range loops keep contiguous registers for OP_FORLOOP_I32 */
    vm_u8 reg_i = c_declare_local(c, has_two_vars ? idx_tok : var1, VM_TYPE_I32, VM_FALSE, (VMReg){0});
    int idx_local_slot = c->num_locals - 1;
    vm_u8 reg_limit = c_alloc_reg(c);

    ExprResult first_expr = c_compile_expr(c, VM_TYPE_I32);

    /* Case A: String character iteration (`for ch in "abc"` or `for i, ch in "abc"`) */
    if (first_expr.struct_id < 0 && first_expr.type == VM_TYPE_STRING) {
        c->locals[idx_local_slot].name = idx_tok.start;
        c->locals[idx_local_slot].length = idx_tok.length;
        vm_u8 reg_ch = c_declare_local(c, val_tok, VM_TYPE_STRING, VM_FALSE, (VMReg){0});
        vm_u8 reg_str = c_alloc_reg(c);
        if (first_expr.reg != reg_str) {
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_str, first_expr.reg, 0));
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRLEN, reg_limit, reg_str, 0));

        vm_u32 loop_start = c->chunk->code_count;
        vm_u8 cmp_reg = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LT_I32, cmp_reg, reg_i, reg_limit));
        vm_u32 exit_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_FALSE, cmp_reg, 0));

        vm_u8 abase = c_alloc_reg(c);
        vm_u8 cnt_reg = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, abase, reg_i, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, cnt_reg, 1));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRSUB, reg_ch, reg_str, abase));

        c_compile_block(c);
        vm_u32 step_ip = c->chunk->code_count;
        while (c->num_continues > prev_continues) {
            vm_u32 c_idx = c->continue_patches[--c->num_continues];
            c->chunk->code[c_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)((vm_i32)step_ip - (vm_i32)(c_idx + 1)));
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ADD_IMM_I32, reg_i, reg_i, 1));
        vm_i16 back_off = (vm_i16)((vm_i32)loop_start - (vm_i32)(c->chunk->code_count + 1));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, back_off));
        c->chunk->code[exit_jmp] = VM_MAKE_ABx(OP_JMP_FALSE, cmp_reg, (vm_i16)(c->chunk->code_count - (exit_jmp + 1)));
        while (c->num_breaks > prev_breaks) {
            vm_u32 b_idx = c->break_patches[--c->num_breaks];
            c->chunk->code[b_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(c->chunk->code_count - (b_idx + 1)));
        }
        c_end_scope(c);
        return;
    }

    /* Case A2: Array iteration (`for x in arr` or `for i, x in arr`) */
    if (first_expr.struct_id < 0 && first_expr.type == VM_TYPE_ARRAY) {
        c->locals[idx_local_slot].name = idx_tok.start;
        c->locals[idx_local_slot].length = idx_tok.length;
        VMDataType elem_t = first_expr.elem_types[0];
        if (elem_t == VM_TYPE_ARRAY || elem_t == VM_TYPE_VOID) elem_t = VM_TYPE_I32;
        vm_u8 reg_val = c_declare_local(c, val_tok, elem_t, VM_FALSE, (VMReg){0});
        vm_u8 reg_arr = c_alloc_reg(c);
        if (first_expr.reg != reg_arr) {
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_arr, first_expr.reg, 0));
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_LEN, reg_limit, reg_arr, 0));

        vm_u32 loop_start = c->chunk->code_count;
        vm_u8 cmp_reg = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LT_I32, cmp_reg, reg_i, reg_limit));
        vm_u32 exit_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_FALSE, cmp_reg, 0));

        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_GET, reg_val, reg_arr, reg_i));

        c_compile_block(c);
        vm_u32 step_ip = c->chunk->code_count;
        while (c->num_continues > prev_continues) {
            vm_u32 c_idx = c->continue_patches[--c->num_continues];
            c->chunk->code[c_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)((vm_i32)step_ip - (vm_i32)(c_idx + 1)));
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ADD_IMM_I32, reg_i, reg_i, 1));
        vm_i16 back_off = (vm_i16)((vm_i32)loop_start - (vm_i32)(c->chunk->code_count + 1));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, back_off));
        c->chunk->code[exit_jmp] = VM_MAKE_ABx(OP_JMP_FALSE, cmp_reg, (vm_i16)(c->chunk->code_count - (exit_jmp + 1)));
        while (c->num_breaks > prev_breaks) {
            vm_u32 b_idx = c->break_patches[--c->num_breaks];
            c->chunk->code[b_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(c->chunk->code_count - (b_idx + 1)));
        }
        c_end_scope(c);
        return;
    }

    /* Case A3: Map iteration (`for k, v in m` or `for k in m`) */
    if (first_expr.struct_id < 0 && first_expr.type == VM_TYPE_MAP) {
        c->locals[idx_local_slot].name = "_map_idx";
        c->locals[idx_local_slot].length = 8;
        VMDataType val_t = first_expr.elem_types[0];
        if (val_t == VM_TYPE_MAP || val_t == VM_TYPE_VOID) val_t = VM_TYPE_I32;
        vm_u8 reg_k = c_declare_local(c, var1, VM_TYPE_STRING, VM_FALSE, (VMReg){0});
        vm_u8 reg_v = 0;
        if (has_two_vars == VM_TRUE) {
            reg_v = c_declare_local(c, var2, val_t, VM_FALSE, (VMReg){0});
        }
        vm_u8 reg_map = c_alloc_reg(c);
        if (first_expr.reg != reg_map) {
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_map, first_expr.reg, 0));
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_LEN, reg_limit, reg_map, 0));

        vm_u32 loop_start = c->chunk->code_count;
        vm_u8 cmp_reg = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LT_I32, cmp_reg, reg_i, reg_limit));
        vm_u32 exit_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_FALSE, cmp_reg, 0));

        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_KEY_AT, reg_k, reg_map, reg_i));
        if (has_two_vars == VM_TRUE) {
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_VAL_AT, reg_v, reg_map, reg_i));
        }

        c_compile_block(c);
        vm_u32 step_ip = c->chunk->code_count;
        while (c->num_continues > prev_continues) {
            vm_u32 c_idx = c->continue_patches[--c->num_continues];
            c->chunk->code[c_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)((vm_i32)step_ip - (vm_i32)(c_idx + 1)));
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ADD_IMM_I32, reg_i, reg_i, 1));
        vm_i16 back_off = (vm_i16)((vm_i32)loop_start - (vm_i32)(c->chunk->code_count + 1));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, back_off));
        c->chunk->code[exit_jmp] = VM_MAKE_ABx(OP_JMP_FALSE, cmp_reg, (vm_i16)(c->chunk->code_count - (exit_jmp + 1)));
        while (c->num_breaks > prev_breaks) {
            vm_u32 b_idx = c->break_patches[--c->num_breaks];
            c->chunk->code[b_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(c->chunk->code_count - (b_idx + 1)));
        }
        c_end_scope(c);
        return;
    }

    /* Case B: Struct iteration via `operator iter` OR fallback to `operator #`/`len` + `operator []` */
    if (first_expr.struct_id >= 0) {
        int sid = first_expr.struct_id;
        vm_u8 nf = c->structs[sid].num_fields;
        c->locals[idx_local_slot].name = idx_tok.start;
        c->locals[idx_local_slot].length = idx_tok.length;

        /* Pin struct instance registers in loop scope */
        vm_u8 s_base = c_alloc_reg(c);
        for (vm_u8 f = 1; f < nf; f++) c_alloc_reg(c);
        for (vm_u8 f = 0; f < nf; f++) {
            if ((vm_u8)(first_expr.reg + f) != (vm_u8)(s_base + f)) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(s_base + f), (vm_u8)(first_expr.reg + f), 0));
            }
        }

        int iter_fid = c_resolve_struct_method(c, sid, "op_iter", 7);
        if (iter_fid >= 0) {
            VMFunctionInfo *fn = &c->chunk->functions[iter_fid];
            VMDataType elem_t = (fn->return_reg_count >= 2) ? fn->return_elem_types[1] : fn->return_type;
            vm_u8 reg_val = c_declare_local(c, val_tok, elem_t, VM_FALSE, (VMReg){0});
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));

            vm_u32 loop_start = c->chunk->code_count;
            vm_u8 ret_slots = (fn->return_reg_count > 0) ? fn->return_reg_count : 2;
            vm_u8 call_base = c_alloc_reg(c);
            for (vm_u8 r = 1; r < ret_slots; r++) c_alloc_reg(c);
            vm_u8 cursor = (vm_u8)(call_base + ret_slots);
            for (vm_u8 f = 0; f < nf; f++) {
                c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(s_base + f), 0));
            }
            c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, reg_i, 0));
            c->next_reg = (vm_u8)(call_base + ret_slots);
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, iter_fid));

            vm_u32 exit_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_FALSE, call_base, 0));
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_val, (vm_u8)(call_base + 1), 0));

            c_compile_block(c);
            vm_u32 step_ip = c->chunk->code_count;
            while (c->num_continues > prev_continues) {
                vm_u32 c_idx = c->continue_patches[--c->num_continues];
                c->chunk->code[c_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)((vm_i32)step_ip - (vm_i32)(c_idx + 1)));
            }
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ADD_IMM_I32, reg_i, reg_i, 1));
            vm_i16 back_off = (vm_i16)((vm_i32)loop_start - (vm_i32)(c->chunk->code_count + 1));
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, back_off));
            c->chunk->code[exit_jmp] = VM_MAKE_ABx(OP_JMP_FALSE, call_base, (vm_i16)(c->chunk->code_count - (exit_jmp + 1)));
            while (c->num_breaks > prev_breaks) {
                vm_u32 b_idx = c->break_patches[--c->num_breaks];
                c->chunk->code[b_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(c->chunk->code_count - (b_idx + 1)));
            }
            c_end_scope(c);
            return;
        }

        /* Fallback: `operator #` (`op_len`) + `operator []` (`op[]`) */
        int len_fid = c_resolve_struct_method(c, sid, "op_len", 6);
        VMDataType idx_types[2] = {first_expr.type, VM_TYPE_I32};
        int idx_sids[2] = {sid, -1};
        int idx_fid = c_resolve_struct_method_overload(c, sid, "op[]", 4, idx_types, idx_sids, 2);
        if (len_fid >= 0 && idx_fid >= 0) {
            VMFunctionInfo *idx_fn = &c->chunk->functions[idx_fid];
            vm_u8 reg_val = c_declare_local(c, val_tok, idx_fn->return_type, VM_FALSE, (VMReg){0});

            /* Compute length once into `reg_limit` */
            vm_u8 lcall = c_alloc_reg(c);
            vm_u8 lcur = (vm_u8)(lcall + 1);
            for (vm_u8 f = 0; f < nf; f++) {
                c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, lcur++, (vm_u8)(s_base + f), 0));
            }
            c->next_reg = (vm_u8)(lcall + 1);
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, lcall, len_fid));
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_limit, lcall, 0));
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));

            vm_u32 loop_start = c->chunk->code_count;
            vm_u8 cmp_reg = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LT_I32, cmp_reg, reg_i, reg_limit));
            vm_u32 exit_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_FALSE, cmp_reg, 0));

            vm_u8 icall = c_alloc_reg(c);
            vm_u8 icur = (vm_u8)(icall + 1);
            for (vm_u8 f = 0; f < nf; f++) {
                c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, icur++, (vm_u8)(s_base + f), 0));
            }
            c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, icur++, reg_i, 0));
            c->next_reg = (vm_u8)(icall + 1);
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, icall, idx_fid));
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_val, icall, 0));

            c_compile_block(c);
            vm_u32 step_ip = c->chunk->code_count;
            while (c->num_continues > prev_continues) {
                vm_u32 c_idx = c->continue_patches[--c->num_continues];
                c->chunk->code[c_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)((vm_i32)step_ip - (vm_i32)(c_idx + 1)));
            }
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ADD_IMM_I32, reg_i, reg_i, 1));
            vm_i16 back_off = (vm_i16)((vm_i32)loop_start - (vm_i32)(c->chunk->code_count + 1));
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, back_off));
            c->chunk->code[exit_jmp] = VM_MAKE_ABx(OP_JMP_FALSE, cmp_reg, (vm_i16)(c->chunk->code_count - (exit_jmp + 1)));
            while (c->num_breaks > prev_breaks) {
                vm_u32 b_idx = c->break_patches[--c->num_breaks];
                c->chunk->code[b_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(c->chunk->code_count - (b_idx + 1)));
            }
            c_end_scope(c);
            return;
        }
        c_error(c, "Struct in 'for' loop must define 'operator iter' or both 'operator #' and 'operator []'");
        c_end_scope(c);
        return;
    }

    /* Case C: Integer range `for i in start..end` or `for i in end` */
    if (c_match(c, TOK_DOTDOT) == VM_TRUE) {
        if (first_expr.reg != reg_i) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_i, first_expr.reg, 0));
        ExprResult end_expr = c_compile_expr(c, VM_TYPE_I32);
        if (end_expr.reg != reg_limit) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_limit, end_expr.reg, 0));
    } else {
        if (first_expr.reg != reg_limit) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_limit, first_expr.reg, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));
    }

    vm_u8 cmp_reg = c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LT_I32, cmp_reg, reg_i, reg_limit));
    vm_u32 skip_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_FALSE, cmp_reg, 0));

    vm_u32 body_start = c->chunk->code_count;
    c_compile_block(c);
    vm_u32 step_ip = c->chunk->code_count;
    while (c->num_continues > prev_continues) {
        vm_u32 c_idx = c->continue_patches[--c->num_continues];
        c->chunk->code[c_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(step_ip - (c_idx + 1)));
    }
    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_FORLOOP_I32, reg_i, (vm_i16)((vm_i32)body_start - (vm_i32)(c->chunk->code_count + 1))));
    c->chunk->code[skip_jmp] = VM_MAKE_ABx(OP_JMP_FALSE, cmp_reg, (vm_i16)(c->chunk->code_count - (skip_jmp + 1)));
    while (c->num_breaks > prev_breaks) {
        vm_u32 b_idx = c->break_patches[--c->num_breaks];
        c->chunk->code[b_idx] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(c->chunk->code_count - (b_idx + 1)));
    }
    c_end_scope(c);
}
