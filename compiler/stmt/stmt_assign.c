#include "stmt.h"
#include "../expr/expr.h"

void c_compile_block(Compiler *c) {
    c_consume(c, TOK_LBRACE, "Expected '{'");
    c_begin_scope(c);
    while (c_check(c, TOK_RBRACE) == VM_FALSE && c_check(c, TOK_EOF) == VM_FALSE) {
        c_compile_statement(c);
    }
    c_consume(c, TOK_RBRACE, "Expected '}'");
    c_end_scope(c);
}

static vm_bool is_assign_or_compound(TokenType t) {
    return vm_bool_from_int(t >= TOK_ASSIGN && t <= TOK_SHR_EQ);
}

void c_compile_statement(Compiler *c) {
    vm_u8 saved_reg = c->next_reg;

    if (c_check(c, TOK_FUNC) == VM_TRUE) {
        c_advance(c);
        Token fn_name = c->cur;
        int anticipated_fid = (int)c->chunk->num_functions;
        c_declare_local(c, fn_name, VM_TYPE_I32, VM_TRUE, (VMReg){0});
        LocalVar *lv = &c->locals[c->num_locals - 1];
        lv->lambda_func_id = anticipated_fid;

        ExprResult inner = c_compile_lambda_expr(c);
        lv->lambda_func_id = inner.lambda_func_id;
        lv->lambda_gen_id = inner.lambda_gen_id;
        return;
    }
    if (c_match(c, TOK_LET) == VM_TRUE)   { c_compile_var_decl(c, VM_FALSE); return; }
    if (c_match(c, TOK_CONST) == VM_TRUE) { c_compile_var_decl(c, VM_TRUE);  return; }
    if (c_match(c, TOK_IF) == VM_TRUE)    { c_compile_if_stmt(c);    c->next_reg = saved_reg; return; }
    if (c_match(c, TOK_MATCH) == VM_TRUE) { c_compile_match_stmt(c); c->next_reg = saved_reg; return; }
    if (c_match(c, TOK_WHILE) == VM_TRUE) { c_compile_while_stmt(c); c->next_reg = saved_reg; return; }
    if (c_match(c, TOK_FOR) == VM_TRUE)   { c_compile_for_stmt(c);   return; }
    if (c_match(c, TOK_BREAK) == VM_TRUE) {
        c->break_patches[c->num_breaks++] = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, 0));
        c_match(c, TOK_SEMICOLON);
        return;
    }
    if (c_match(c, TOK_CONTINUE) == VM_TRUE) {
        c->continue_patches[c->num_continues++] = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, 0));
        c_match(c, TOK_SEMICOLON);
        return;
    }
    if (c_match(c, TOK_PRINT) == VM_TRUE) {
        c_consume(c, TOK_LPAREN, "Expected '(' after print");
        ExprResult val = c_compile_expr(c, VM_TYPE_I32);
        c_consume(c, TOK_RPAREN, "Expected ')'");
        c_match(c, TOK_SEMICOLON);
        if (val.type == VM_TYPE_ARRAY || val.type == VM_TYPE_MAP) {
            vm_u8 sreg = c_alloc_reg(c);
            vm_u8 tpair = (vm_u8)(((vm_u8)val.type << 4) | ((vm_u8)VM_TYPE_STRING & 0x0Fu));
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CAST, sreg, val.reg, tpair));
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_PRINT_STR, sreg, 0, 0));
        } else {
            VMOpCode pop = (val.type == VM_TYPE_I64) ? OP_PRINT_I64 :
                           (val.type == VM_TYPE_F32) ? OP_PRINT_F32 :
                           (val.type == VM_TYPE_F64) ? OP_PRINT_F64 :
                           (val.type == VM_TYPE_BOOL) ? OP_PRINT_BOOL :
                           (val.type == VM_TYPE_STRING) ? OP_PRINT_STR : OP_PRINT_I32;
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(pop, val.reg, 0, 0));
        }
        c->next_reg = saved_reg;
        return;
    }
    if (c_match(c, TOK_RETURN) == VM_TRUE) {
        if (c_check(c, TOK_RBRACE) == VM_TRUE || c_match(c, TOK_SEMICOLON) == VM_TRUE) {
            c_emit_defers_for_return(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        } else {
            ExprResult val = c_compile_expr(c, c->current_ret_type);
            if (c_match(c, TOK_COMMA) == VM_TRUE) {
                vm_u8 base_reg = c_alloc_reg(c);
                if (val.reg != base_reg) {
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, base_reg, val.reg, 0));
                }
                val.reg = base_reg;
                val.elem_count = 1;
                do {
                    vm_u8 slot = c_alloc_reg(c);
                    vm_u8 save = c->next_reg;
                    ExprResult next_e = c_compile_expr(c, VM_TYPE_I32);
                    c->next_reg = save;
                    if (next_e.reg != slot) {
                        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, slot, next_e.reg, 0));
                    }
                    val.elem_types[val.elem_count++] = next_e.type;
                } while (c_match(c, TOK_COMMA) == VM_TRUE);
            }
            if (c->current_ret_struct_id >= 0 && val.struct_id >= 0) {
                (void)c_check_no_object_slicing(c, c->current_ret_struct_id, val.struct_id);
            }
            c_match(c, TOK_SEMICOLON);
            vm_u8 ret_count = (val.struct_id >= 0) ? c->structs[val.struct_id].num_fields : val.elem_count;
            if (ret_count == 0) ret_count = 1;
            if (c->next_reg < (vm_u8)(val.reg + ret_count)) {
                c->next_reg = (vm_u8)(val.reg + ret_count);
                if (c->next_reg > c->max_reg) c->max_reg = c->next_reg;
            }
            c_emit_defers_for_return(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, val.reg, ret_count, 0));
        }
        c->next_reg = saved_reg;
        return;
    }
    if (c_check(c, TOK_LBRACE) == VM_TRUE) { c_compile_block(c); return; }

    if (c_check(c, TOK_IDENT) == VM_TRUE) {
        Token id = c->cur;
        Token next = lexer_peek(&c->lex);

        if (vm_str_eq_cstr(id.start, id.length, "defer") == VM_TRUE &&
            next.type != TOK_DOT && next.type != TOK_LPAREN &&
            is_assign_or_compound(next.type) == VM_FALSE) {
            c_advance(c); /* consume 'defer' */
            if (c->num_defers < 32) {
                int didx = c->num_defers++;
                c->defer_src[didx] = c->cur.start;
                c->defer_line[didx] = c->cur.line;
                c->defer_scope_depth[didx] = c->scope_depth;
            }
            if (c_check(c, TOK_LBRACE) == VM_TRUE) {
                int depth = 0;
                do {
                    if (c_check(c, TOK_LBRACE) == VM_TRUE) depth++;
                    else if (c_check(c, TOK_RBRACE) == VM_TRUE) depth--;
                    c_advance(c);
                } while (depth > 0 && c_check(c, TOK_EOF) == VM_FALSE);
            } else {
                while (c_check(c, TOK_SEMICOLON) == VM_FALSE &&
                       c_check(c, TOK_RBRACE) == VM_FALSE &&
                       c_check(c, TOK_EOF) == VM_FALSE) {
                    c_advance(c);
                }
                c_match(c, TOK_SEMICOLON);
            }
            return;
        }

        if (next.type == TOK_LBRACKET) {
            int loc = c_resolve_local(c, id.start, id.length);
            if (loc >= 0 && c->locals[loc].struct_id < 0 && c->locals[loc].type == VM_TYPE_ARRAY) {
                c_advance(c); /* consume id */
                c_advance(c); /* consume '[' */
                ExprResult idx = c_compile_expr(c, VM_TYPE_I32);
                c_consume(c, TOK_RBRACKET, "Expected ']' in array index assignment");
                c_consume(c, TOK_ASSIGN, "Expected '=' after ']' in array index assignment");
                VMDataType et = c->locals[loc].elem_types[0];
                if (et == VM_TYPE_ARRAY || et == VM_TYPE_VOID) et = VM_TYPE_I32;
                ExprResult val = c_compile_expr(c, et);
                c->locals[loc].elem_types[0] = val.type;
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_SET, c->locals[loc].reg, idx.reg, val.reg));
                c_match(c, TOK_SEMICOLON);
                c->next_reg = saved_reg;
                return;
            }
            if (loc >= 0 && c->locals[loc].struct_id < 0 && c->locals[loc].type == VM_TYPE_MAP) {
                c_advance(c); /* consume id */
                c_advance(c); /* consume '[' */
                ExprResult key = c_compile_expr(c, VM_TYPE_STRING);
                c_consume(c, TOK_RBRACKET, "Expected ']' in map index assignment");
                c_consume(c, TOK_ASSIGN, "Expected '=' after ']' in map index assignment");
                VMDataType vt = c->locals[loc].elem_types[0];
                if (vt == VM_TYPE_MAP || vt == VM_TYPE_VOID) vt = VM_TYPE_I32;
                ExprResult val = c_compile_expr(c, vt);
                c->locals[loc].elem_types[0] = val.type;
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_SET, c->locals[loc].reg, key.reg, val.reg));
                c_match(c, TOK_SEMICOLON);
                c->next_reg = saved_reg;
                return;
            }
            if (loc >= 0 && c->locals[loc].struct_id >= 0) {
                c_advance(c); /* consume id */
                c_advance(c); /* consume '[' */
                int sid = c->locals[loc].struct_id;
                ExprResult key = c_compile_expr(c, VM_TYPE_I32);
                c_consume(c, TOK_RBRACKET, "Expected ']' in index assignment");
                c_consume(c, TOK_ASSIGN, "Expected '=' after ']' in index assignment");
                ExprResult val = c_compile_expr(c, VM_TYPE_I32);
                VMDataType arg_types[3] = {c->locals[loc].type, key.type, val.type};
                int arg_sids[3] = {sid, key.struct_id, val.struct_id};
                int fid = c_resolve_struct_method_overload(c, sid, "op[]=", 5, arg_types, arg_sids, 3);
                if (fid < 0) {
                    c_error(c, "Struct does not define matching 'operator []=' for index assignment");
                    return;
                }
                VMFunctionInfo *fn = &c->chunk->functions[fid];
                vm_u8 ret_slots = (fn->return_reg_count > 0) ? fn->return_reg_count : 1;
                vm_u8 call_base = c_alloc_reg(c);
                for (vm_u8 r = 1; r < ret_slots; r++) c_alloc_reg(c);
                vm_u8 cursor = (vm_u8)(call_base + ret_slots);
                vm_u8 nf = c->structs[sid].num_fields;
                for (vm_u8 f = 0; f < nf; f++) {
                    c_alloc_reg(c);
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(c->locals[loc].reg + f), 0));
                }
                vm_u8 kcount = (key.struct_id >= 0) ? c->structs[key.struct_id].num_fields : 1;
                for (vm_u8 k = 0; k < kcount; k++) {
                    c_alloc_reg(c);
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(key.reg + k), 0));
                }
                vm_u8 vcount = (val.struct_id >= 0) ? c->structs[val.struct_id].num_fields : 1;
                for (vm_u8 v = 0; v < vcount; v++) {
                    c_alloc_reg(c);
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(val.reg + v), 0));
                }
                c->next_reg = (vm_u8)(call_base + ret_slots);
                vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, fid));
                if (fn->return_struct_id == sid) {
                    for (vm_u8 f = 0; f < nf; f++) {
                        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(c->locals[loc].reg + f), (vm_u8)(call_base + f), 0));
                    }
                }
                c_match(c, TOK_SEMICOLON);
                c->next_reg = saved_reg;
                return;
            }
        }

        if (next.type == TOK_DOT) {
            int loc = c_resolve_local(c, id.start, id.length);
            if (loc >= 0 && c->locals[loc].struct_id >= 0) {
                int sid = c->locals[loc].struct_id;
                Lexer check_lex = c->lex;
                (void)lexer_next(&check_lex);
                Token member_tok = lexer_next(&check_lex);
                Token after_member = lexer_next(&check_lex);
                if (is_assign_or_compound(after_member.type) == VM_TRUE) {
                    c_advance(c); c_advance(c); c_advance(c);
                    TokenType f_op = c->cur.type;
                    c_advance(c);
                    StructDef *sd = &c->structs[sid];
                    for (vm_u8 f = 0; f < sd->num_fields; f++) {
                        if (vm_str_eq_cstr(member_tok.start, member_tok.length, sd->field_names[f]) == VM_TRUE) {
                            if (c_can_access_struct_member(c, sid, sd->field_is_pub[f]) == VM_FALSE) {
                                c_error(c, "Cannot modify private struct field (missing 'pub') from outside its struct");
                            }
                            vm_u8 target_reg = (vm_u8)(c->locals[loc].reg + f);
                            VMDataType ftype = sd->field_types[f];
                            ExprResult rhs = c_compile_expr(c, ftype);
                            if (f_op == TOK_ASSIGN) {
                                if (rhs.reg != target_reg) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, target_reg, rhs.reg, 0));
                            } else if (f_op == TOK_PLUS_EQ && ftype == VM_TYPE_STRING) {
                                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRCAT, target_reg, target_reg, rhs.reg));
                            } else {
                                VMOpCode op = (f_op == TOK_PLUS_EQ)  ? ((ftype == VM_TYPE_F32) ? OP_ADD_F32 : (ftype == VM_TYPE_F64) ? OP_ADD_F64 : (ftype == VM_TYPE_I64) ? OP_ADD_I64 : OP_ADD_I32) :
                                              (f_op == TOK_MINUS_EQ) ? ((ftype == VM_TYPE_F32) ? OP_SUB_F32 : (ftype == VM_TYPE_F64) ? OP_SUB_F64 : (ftype == VM_TYPE_I64) ? OP_SUB_I64 : OP_SUB_I32) :
                                              (f_op == TOK_STAR_EQ)  ? ((ftype == VM_TYPE_F32) ? OP_MUL_F32 : (ftype == VM_TYPE_F64) ? OP_MUL_F64 : (ftype == VM_TYPE_I64) ? OP_MUL_I64 : OP_MUL_I32) :
                                              (f_op == TOK_SLASH_EQ) ? ((ftype == VM_TYPE_F32) ? OP_DIV_F32 : (ftype == VM_TYPE_F64) ? OP_DIV_F64 : (ftype == VM_TYPE_I64) ? OP_DIV_I64 : OP_DIV_I32) :
                                              (f_op == TOK_MOD_EQ)   ? OP_MOD_I32 :
                                              (f_op == TOK_BAND_EQ)  ? OP_BAND_I32 :
                                              (f_op == TOK_BOR_EQ)   ? OP_BOR_I32 :
                                              (f_op == TOK_BXOR_EQ)  ? OP_BXOR_I32 :
                                              (f_op == TOK_SHL_EQ)   ? OP_SHL_I32 : OP_SHR_I32;
                                vm_chunk_emit(c->chunk, VM_MAKE_ABC(op, target_reg, target_reg, rhs.reg));
                            }
                            c_match(c, TOK_SEMICOLON);
                            c->next_reg = saved_reg;
                            return;
                        }
                    }
                }
            }
        }

        if (is_assign_or_compound(next.type) == VM_TRUE) {
            c_advance(c);
            TokenType assign_op = c->cur.type;
            c_advance(c);
            int loc = c_resolve_local(c, id.start, id.length);
            if (loc < 0 || c->locals[loc].is_const == VM_TRUE) {
                c_error(c, "Invalid assignment target");
                return;
            }
            vm_u8 target_reg = c->locals[loc].reg;
            VMDataType target_type = c->locals[loc].type;

            if (assign_op == TOK_ASSIGN) {
                ExprResult rhs = c_compile_expr(c, target_type);
                if (target_type == VM_TYPE_ARRAY || target_type == VM_TYPE_MAP) {
                    c->locals[loc].elem_types[0] = rhs.elem_types[0];
                }
                if (c->locals[loc].struct_id >= 0) {
                    if (c_check_no_object_slicing(c, c->locals[loc].struct_id, rhs.struct_id) == VM_TRUE) {
                        vm_u8 nf = c->structs[c->locals[loc].struct_id].num_fields;
                        for (vm_u8 f = 0; f < nf; f++) {
                            if ((vm_u8)(rhs.reg + f) != (vm_u8)(target_reg + f)) {
                                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(target_reg + f), (vm_u8)(rhs.reg + f), 0));
                            }
                        }
                    }
                } else if (rhs.reg != target_reg) {
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, target_reg, rhs.reg, 0));
                }
            } else {
                vm_u32 before_rhs = c->chunk->code_count;
                ExprResult rhs = c_compile_expr(c, target_type);
                if (c->locals[loc].struct_id >= 0) {
                    const char *op_name = (assign_op == TOK_PLUS_EQ)  ? "op+"  :
                                          (assign_op == TOK_MINUS_EQ) ? "op-"  :
                                          (assign_op == TOK_STAR_EQ)  ? "op*"  :
                                          (assign_op == TOK_SLASH_EQ) ? "op/"  :
                                          (assign_op == TOK_MOD_EQ)   ? "op%"  :
                                          (assign_op == TOK_BAND_EQ)  ? "op&"  :
                                          (assign_op == TOK_BOR_EQ)   ? "op|"  :
                                          (assign_op == TOK_BXOR_EQ)  ? "op^"  :
                                          (assign_op == TOK_SHL_EQ)   ? "op<<" : "op>>";
                    int sid = c->locals[loc].struct_id;
                    VMDataType arg_types[2] = {target_type, rhs.type};
                    int arg_sids[2] = {sid, rhs.struct_id};
                    int fid = c_resolve_struct_method_overload(c, sid, op_name, vm_str_len(op_name), arg_types, arg_sids, 2);
                    if (fid < 0) {
                        c_error(c, "Struct does not define matching operator for compound assignment");
                        return;
                    }
                    VMFunctionInfo *fn = &c->chunk->functions[fid];
                    vm_u8 ret_slots = (fn->return_reg_count > 0) ? fn->return_reg_count : 1;
                    vm_u8 call_base = c_alloc_reg(c);
                    for (vm_u8 r = 1; r < ret_slots; r++) c_alloc_reg(c);
                    vm_u8 cursor = (vm_u8)(call_base + ret_slots);
                    vm_u8 nf = c->structs[sid].num_fields;
                    for (vm_u8 f = 0; f < nf; f++) {
                        c_alloc_reg(c);
                        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(target_reg + f), 0));
                    }
                    vm_u8 rcount = (rhs.struct_id >= 0) ? c->structs[rhs.struct_id].num_fields : 1;
                    for (vm_u8 k = 0; k < rcount; k++) {
                        c_alloc_reg(c);
                        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(rhs.reg + k), 0));
                    }
                    c->next_reg = (vm_u8)(call_base + ret_slots);
                    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, fid));
                    for (vm_u8 f = 0; f < nf; f++) {
                        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(target_reg + f), (vm_u8)(call_base + f), 0));
                    }
                } else if (assign_op == TOK_PLUS_EQ && target_type == VM_TYPE_STRING) {
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRCAT, target_reg, target_reg, rhs.reg));
                } else if ((assign_op == TOK_PLUS_EQ || assign_op == TOK_MINUS_EQ) &&
                           target_type == VM_TYPE_I32 && rhs.is_const == VM_TRUE &&
                           rhs.const_val.i32 >= -127 && rhs.const_val.i32 <= 127) {
                    c->chunk->code_count = before_rhs;
                    vm_i8 imm = (assign_op == TOK_PLUS_EQ) ? (vm_i8)rhs.const_val.i32 : (vm_i8)(-rhs.const_val.i32);
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ADD_IMM_I32, target_reg, target_reg, (vm_u8)imm));
                } else {
                    VMOpCode op = (assign_op == TOK_PLUS_EQ)  ? ((target_type == VM_TYPE_F32) ? OP_ADD_F32 : (target_type == VM_TYPE_F64) ? OP_ADD_F64 : (target_type == VM_TYPE_I64) ? OP_ADD_I64 : OP_ADD_I32) :
                                  (assign_op == TOK_MINUS_EQ) ? ((target_type == VM_TYPE_F32) ? OP_SUB_F32 : (target_type == VM_TYPE_F64) ? OP_SUB_F64 : (target_type == VM_TYPE_I64) ? OP_SUB_I64 : OP_SUB_I32) :
                                  (assign_op == TOK_STAR_EQ)  ? ((target_type == VM_TYPE_F32) ? OP_MUL_F32 : (target_type == VM_TYPE_F64) ? OP_MUL_F64 : (target_type == VM_TYPE_I64) ? OP_MUL_I64 : OP_MUL_I32) :
                                  (assign_op == TOK_SLASH_EQ) ? ((target_type == VM_TYPE_F32) ? OP_DIV_F32 : (target_type == VM_TYPE_F64) ? OP_DIV_F64 : (target_type == VM_TYPE_I64) ? OP_DIV_I64 : OP_DIV_I32) :
                                  (assign_op == TOK_MOD_EQ)   ? OP_MOD_I32 :
                                  (assign_op == TOK_BAND_EQ)  ? OP_BAND_I32 :
                                  (assign_op == TOK_BOR_EQ)   ? OP_BOR_I32 :
                                  (assign_op == TOK_BXOR_EQ)  ? OP_BXOR_I32 :
                                  (assign_op == TOK_SHL_EQ)   ? OP_SHL_I32 : OP_SHR_I32;
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(op, target_reg, target_reg, rhs.reg));
                }
            }
            c_match(c, TOK_SEMICOLON);
            c->next_reg = saved_reg;
            return;
        }
    }

    (void)c_compile_expr(c, VM_TYPE_VOID);
    c_match(c, TOK_SEMICOLON);
    c->next_reg = saved_reg;
}
