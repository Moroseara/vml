#include "expr.h"
#include "../decl/decl.h"

static vm_u8 coerce_expr_to_string_reg(Compiler *c, ExprResult val) {
    if (val.struct_id >= 0) {
        int str_fid = c_resolve_struct_method(c, val.struct_id, "op_str", 6);
        if (str_fid >= 0) {
            int sid = val.struct_id;
            vm_u8 call_base = c_alloc_reg(c);
            vm_u8 cursor = (vm_u8)(call_base + 1);
            vm_u8 nf = c->structs[sid].num_fields;
            for (vm_u8 f = 0; f < nf; f++) {
                c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(val.reg + f), 0));
            }
            c->next_reg = (vm_u8)(call_base + 1);
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, str_fid));
            return call_base;
        }
    }
    if (val.type != VM_TYPE_STRING) {
        vm_u8 dst = c_alloc_reg(c);
        vm_u8 tpair = (vm_u8)(((vm_u8)val.type << 4) | ((vm_u8)VM_TYPE_STRING & 0x0Fu));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CAST, dst, val.reg, tpair));
        return dst;
    }
    return val.reg;
}

static ExprResult emit_overloaded_call(Compiler *c, const char *qname, vm_u32 qlen,
                                       int fixed_fid, int struct_sid,
                                       const char *mname, vm_u32 mlen,
                                       const ExprResult *self_arg) {
    ExprResult res = {0, VM_TYPE_I32, VM_FALSE, {0}, 1, {VM_TYPE_I32}, -1, -1, -1, VM_FALSE, VM_FALSE};

    ExprResult args[16];
    VMDataType arg_types[16];
    int        arg_sids[16];
    vm_u8      argc = 0;

    if (self_arg != VM_NULL) {
        args[0] = *self_arg;
        arg_types[0] = self_arg->type;
        arg_sids[0] = self_arg->struct_id;
        argc = 1;
    }

    if (c_check(c, TOK_RPAREN) == VM_FALSE) {
        do {
            args[argc] = c_compile_expr(c, VM_TYPE_I32);
            arg_types[argc] = args[argc].type;
            arg_sids[argc] = args[argc].struct_id;
            argc++;
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
    }
    c_consume(c, TOK_RPAREN, "Expected ')' after arguments");

    int fid = fixed_fid;
    if (fid < 0 && struct_sid >= 0) {
        fid = c_resolve_struct_method_overload(c, struct_sid, mname, mlen, arg_types, arg_sids, argc);
    }
    if (fid < 0) {
        fid = c_resolve_func_overload(c, qname, qlen, arg_types, arg_sids, argc);
    }
    /* Automatic `operator string` (__tostring) conversion when passing a struct to a string function (e.g. io.println) */
    if (fid < 0 && argc == 1 && args[0].struct_id >= 0) {
        int str_fid = c_resolve_struct_method(c, args[0].struct_id, "op_str", 6);
        if (str_fid >= 0) {
            int sid = args[0].struct_id;
            vm_u8 str_base = c_alloc_reg(c);
            vm_u8 scursor = (vm_u8)(str_base + 1);
            vm_u8 nf = c->structs[sid].num_fields;
            for (vm_u8 f = 0; f < nf; f++) {
                c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, scursor++, (vm_u8)(args[0].reg + f), 0));
            }
            c->next_reg = (vm_u8)(str_base + 1);
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, str_base, str_fid));
            args[0].reg = str_base;
            args[0].type = VM_TYPE_STRING;
            args[0].struct_id = -1;
            args[0].elem_count = 1;
            arg_types[0] = VM_TYPE_STRING;
            arg_sids[0] = -1;
            fid = c_resolve_func_overload(c, qname, qlen, arg_types, arg_sids, argc);
        }
    }
    if (fid < 0) {
        char err_msg[128];
        const char *target = (qlen > 0) ? qname : mname;
        vm_u32 tlen = (qlen > 0) ? qlen : mlen;
        vm_str_join_dot(err_msg, 128, "No matching function or argument types for call to", 50, target, tlen);
        c_error(c, err_msg);
        return res;
    }

    VMFunctionInfo *fn = &c->chunk->functions[fid];
    if (fn->is_deprecated == VM_TRUE) {
        vm_sys_write_cstr("[Deprecated Warning] ");
        vm_sys_write_cstr(fn->name);
        vm_sys_write_cstr(": ");
        vm_sys_write_cstr(fn->deprecated_msg);
        vm_sys_write_cstr("\n");
    }
    if (c_can_access_struct_member(c, fn->owner_struct_id, fn->is_pub) == VM_FALSE) {
        c_error(c, "Cannot call private struct method (missing 'pub') from outside its struct");
        return res;
    }

    if (fn->coerce_args_to_string == VM_TRUE) {
        for (vm_u8 i = 1; i < argc; i++) {
            args[i].reg = coerce_expr_to_string_reg(c, args[i]);
            args[i].type = VM_TYPE_STRING;
            args[i].struct_id = -1;
            args[i].elem_count = 1;
        }
    }

    vm_u8 ret_slots = (fn->return_reg_count > 0) ? fn->return_reg_count : 1;

    vm_u8 call_base = c_alloc_reg(c);
    for (vm_u8 r = 1; r < ret_slots; r++) c_alloc_reg(c);

    vm_u8 param_cursor = (vm_u8)(call_base + ret_slots);
    for (vm_u8 i = 0; i < fn->arity; i++) {
        if (i < argc) {
            vm_u8 count = 1;
            vm_u8 field_off = 0;
            if (fn->param_struct_ids[i] >= 0) {
                int psid = fn->param_struct_ids[i];
                count = c->structs[psid].num_fields;
                if (args[i].struct_id >= 0 && args[i].struct_id != psid && c->structs[psid].num_fields > 0) {
                    const char *first_fname = c->structs[psid].field_names[0];
                    vm_u32 fflen = vm_str_len(first_fname);
                    StructDef *child_sd = &c->structs[args[i].struct_id];
                    for (vm_u8 cf = 0; cf < child_sd->num_fields; cf++) {
                        if (vm_str_eq(child_sd->field_names[cf], vm_str_len(child_sd->field_names[cf]), first_fname, fflen) == VM_TRUE) {
                            field_off = cf;
                            break;
                        }
                    }
                }
            } else if (args[i].struct_id >= 0) {
                count = c->structs[args[i].struct_id].num_fields;
            }
            for (vm_u8 k = 0; k < count; k++) {
                vm_u8 dst = c_alloc_reg(c);
                if (dst != param_cursor) dst = param_cursor;
                vm_u8 src_r = (vm_u8)(args[i].reg + field_off + k);
                if (src_r != param_cursor) {
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, param_cursor, src_r, 0));
                }
                param_cursor++;
            }
        } else {
            vm_u8 dst = c_alloc_reg(c);
            if (dst != param_cursor) dst = param_cursor;
            vm_u8 kreg = c_emit_load_const(c, fn->param_defaults[i], fn->param_types[i]);
            if (kreg != param_cursor) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, param_cursor, kreg, 0));
            }
            param_cursor++;
        }
    }

    c->next_reg = (vm_u8)(call_base + ret_slots);
    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, fid));

    res.reg = call_base;
    res.type = fn->return_type;
    res.struct_id = fn->return_struct_id;
    res.elem_count = ret_slots;
    if (res.struct_id >= 0) {
        StructDef *sd = &c->structs[res.struct_id];
        for (vm_u8 f = 0; f < sd->num_fields; f++) res.elem_types[f] = sd->field_types[f];
    } else if (ret_slots > 1) {
        for (vm_u8 r = 0; r < ret_slots; r++) res.elem_types[r] = fn->return_elem_types[r];
    } else {
        res.elem_types[0] = res.type;
    }
    return res;
}

vm_bool c_is_postfix_method_call(Compiler *c) {
    if (c_check(c, TOK_COLON) == VM_FALSE) return VM_FALSE;
    Lexer lcopy = c->lex;
    Token m = lexer_next(&lcopy);
    if (m.type != TOK_IDENT && m.type != TOK_LEN &&
        !(m.type >= TOK_TYPE_I32 && m.type <= TOK_TYPE_VOID)) {
        return VM_FALSE;
    }
    Token lp = lexer_next(&lcopy);
    return vm_bool_from_int(lp.type == TOK_LPAREN);
}

ExprResult c_compile_postfix_method(Compiler *c, ExprResult self_expr) {
    Token mname = c->cur;
    if (c->cur.type == TOK_IDENT || c->cur.type == TOK_LEN ||
        (c->cur.type >= TOK_TYPE_I32 && c->cur.type <= TOK_TYPE_VOID)) {
        c_advance(c);
    } else {
        c_consume(c, TOK_IDENT, "Expected method name after ':'");
    }
    c_consume(c, TOK_LPAREN, "Expected '(' after method name");

    if (self_expr.struct_id >= 0) {
        return emit_overloaded_call(c, "", 0, -1, self_expr.struct_id, mname.start, mname.length, &self_expr);
    }
    if ((int)self_expr.type >= 0 && (int)self_expr.type < VM_TYPE_COUNT) {
        const char *pname = c->chunk->primitive_struct_name[self_expr.type];
        if (pname[0] != '\0') {
            int psid = c_resolve_struct(c, pname, vm_str_len(pname));
            if (psid >= 0) {
                char sqname[64];
                vm_str_join_dot(sqname, 64, pname, vm_str_len(pname), mname.start, mname.length);
                return emit_overloaded_call(c, sqname, vm_str_len(sqname), -1, psid, mname.start, mname.length, &self_expr);
            }
        }
    }
    c_error(c, "':' method call requires a struct or primitive type with bound struct");
    return self_expr;
}

ExprResult c_compile_ident_or_call(Compiler *c, Token id) {
    ExprResult res = {0, VM_TYPE_I32, VM_FALSE, {0}, 1, {VM_TYPE_I32}, -1, -1, -1, VM_FALSE, VM_FALSE};

    /* Lua `__index` equivalent: `id[key]` via `operator []`, `arr[i]`, `map[k]`, or 1-char string slice `s[i]` */
    if (c_match(c, TOK_LBRACKET) == VM_TRUE) {
        int loc = c_resolve_local(c, id.start, id.length);
        if (loc >= 0 && c->locals[loc].struct_id < 0 && c->locals[loc].type == VM_TYPE_STRING) {
            ExprResult idx = c_compile_expr(c, VM_TYPE_I32);
            c_consume(c, TOK_RBRACKET, "Expected ']' after string index");
            vm_u8 abase = c_alloc_reg(c);
            vm_u8 cnt_reg = c_alloc_reg(c);
            if (idx.reg != abase) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, abase, idx.reg, 0));
            }
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, cnt_reg, 1));
            vm_u8 dst = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRSUB, dst, c->locals[loc].reg, abase));
            res.reg = dst;
            res.type = VM_TYPE_STRING;
            res.elem_types[0] = VM_TYPE_STRING;
            return res;
        }
        if (loc >= 0 && c->locals[loc].struct_id < 0 && c->locals[loc].type == VM_TYPE_ARRAY) {
            ExprResult idx = c_compile_expr(c, VM_TYPE_I32);
            c_consume(c, TOK_RBRACKET, "Expected ']' after array index");
            vm_u8 dst = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_GET, dst, c->locals[loc].reg, idx.reg));
            VMDataType et = c->locals[loc].elem_types[0];
            if (et == VM_TYPE_ARRAY || et == VM_TYPE_VOID) et = VM_TYPE_I32;
            res.reg = dst;
            res.type = et;
            res.elem_types[0] = et;
            return res;
        }
        if (loc >= 0 && c->locals[loc].struct_id < 0 && c->locals[loc].type == VM_TYPE_MAP) {
            ExprResult key = c_compile_expr(c, VM_TYPE_STRING);
            c_consume(c, TOK_RBRACKET, "Expected ']' after map key");
            vm_u8 dst = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_GET, dst, c->locals[loc].reg, key.reg));
            VMDataType vt = c->locals[loc].elem_types[0];
            if (vt == VM_TYPE_MAP || vt == VM_TYPE_VOID) vt = VM_TYPE_I32;
            res.reg = dst;
            res.type = vt;
            res.elem_types[0] = vt;
            return res;
        }
        if (loc < 0 || c->locals[loc].struct_id < 0) {
            c_error(c, "'[]' index operator requires an array, map, string, or struct with 'operator []'");
            return res;
        }
        int sid = c->locals[loc].struct_id;
        ExprResult key = c_compile_expr(c, VM_TYPE_I32);
        c_consume(c, TOK_RBRACKET, "Expected ']' after index expression");
        VMDataType arg_types[2] = {c->locals[loc].type, key.type};
        int arg_sids[2] = {sid, key.struct_id};
        int fid = c_resolve_struct_method_overload(c, sid, "op[]", 4, arg_types, arg_sids, 2);
        if (fid < 0) {
            c_error(c, "Struct does not define matching 'operator []'");
            return res;
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
        c->next_reg = (vm_u8)(call_base + ret_slots);
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, fid));
        res.reg = call_base;
        res.type = fn->return_type;
        res.struct_id = fn->return_struct_id;
        res.elem_count = ret_slots;
        return res;
    }

    if (c_check(c, TOK_LT) == VM_TRUE) {
        int gen_id = c_resolve_generic(c, id.start, id.length);
        int loc = c_resolve_local(c, id.start, id.length);
        if (gen_id < 0 && loc >= 0 && c->locals[loc].lambda_gen_id >= 0) {
            gen_id = c->locals[loc].lambda_gen_id;
        }
        if (gen_id >= 0) {
            c_advance(c);
            VMDataType targs[8];
            vm_u8 num_targs = 0;
            do {
                if (c_check(c, TOK_IDENT) == VM_TRUE && lexer_peek(&c->lex).type == TOK_COLON) {
                    c_advance(c);
                    c_advance(c);
                }
                targs[num_targs++] = c_parse_type(c);
            } while (c_match(c, TOK_COMMA) == VM_TRUE);
            c_consume(c, TOK_GT, "Expected '>'");
            c_consume(c, TOK_LPAREN, "Expected '('");
            int fid = c_instantiate_generic(c, gen_id, targs, num_targs);
            return emit_overloaded_call(c, "", 0, fid, -1, "", 0, VM_NULL);
        }
    }

    if (c_match(c, TOK_DOT) == VM_TRUE) {
        if (c_check(c, TOK_INT_LIT) == VM_TRUE) {
            vm_u8 elem_idx = (vm_u8)c->cur.int_val;
            c_advance(c);
            int loc = c_resolve_local(c, id.start, id.length);
            if (loc >= 0) {
                res.reg = (vm_u8)(c->locals[loc].reg + elem_idx);
                res.type = (elem_idx < c->locals[loc].elem_count) ? c->locals[loc].elem_types[elem_idx] : c->locals[loc].type;
                return res;
            }
        }

        Token ns_tok = id;
        Token member = c->cur;
        if (c->cur.type == TOK_IDENT || c->cur.type == TOK_LEN ||
            (c->cur.type >= TOK_TYPE_I32 && c->cur.type <= TOK_TYPE_VOID)) {
            c_advance(c);
        } else {
            c_consume(c, TOK_IDENT, "Expected member name after '.'");
        }
        if (c_match(c, TOK_DOT) == VM_TRUE) {
            ns_tok = member;
            member = c->cur;
            if (c->cur.type == TOK_IDENT || c->cur.type == TOK_LEN ||
                (c->cur.type >= TOK_TYPE_I32 && c->cur.type <= TOK_TYPE_VOID)) {
                c_advance(c);
            } else {
                c_consume(c, TOK_IDENT, "Expected member name after '.'");
            }
        }

        char qname[64];
        vm_str_join_dot(qname, 64, ns_tok.start, ns_tok.length, member.start, member.length);
        vm_u32 qlen = vm_str_len(qname);
        int ev = c_resolve_enum_variant(c, qname, qlen);

        if (c_match(c, TOK_LPAREN) == VM_TRUE) {
            if (vm_str_eq_cstr(ns_tok.start, ns_tok.length, "Math") == VM_TRUE &&
                c_check(c, TOK_RPAREN) == VM_FALSE) {
                int mkind = -1;
                if (vm_str_eq_cstr(member.start, member.length, "sqrt") == VM_TRUE)  mkind = 0;
                else if (vm_str_eq_cstr(member.start, member.length, "floor") == VM_TRUE) mkind = 1;
                else if (vm_str_eq_cstr(member.start, member.length, "ceil") == VM_TRUE)  mkind = 2;
                else if (vm_str_eq_cstr(member.start, member.length, "sin") == VM_TRUE)   mkind = 3;
                else if (vm_str_eq_cstr(member.start, member.length, "cos") == VM_TRUE)   mkind = 4;
                else if (vm_str_eq_cstr(member.start, member.length, "abs") == VM_TRUE)   mkind = 5;
                if (mkind >= 0) {
                    ExprResult marg = c_compile_expr(c, (mkind == 5) ? VM_TYPE_I32 : VM_TYPE_F32);
                    c_consume(c, TOK_RPAREN, "Expected ')' after Math intrinsic argument");
                    vm_u8 op_kind = (vm_u8)mkind;
                    if (mkind == 5 && marg.type != VM_TYPE_F32) op_kind = 6;
                    vm_u8 dst = c_alloc_reg(c);
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MATH, dst, marg.reg, op_kind));
                    res.reg = dst;
                    res.type = (op_kind == 6) ? VM_TYPE_I32 : VM_TYPE_F32;
                    res.elem_types[0] = res.type;
                    return res;
                }
            }
            if (vm_str_eq_cstr(ns_tok.start, ns_tok.length, "Bit") == VM_TRUE &&
                vm_str_eq_cstr(member.start, member.length, "count_ones") == VM_TRUE) {
                ExprResult barg = c_compile_expr(c, VM_TYPE_I32);
                c_consume(c, TOK_RPAREN, "Expected ')' after Bit.count_ones argument");
                vm_u8 dst = c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MATH, dst, barg.reg, 7));
                res.reg = dst;
                res.type = VM_TYPE_I32;
                return res;
            }
            if ((vm_str_eq_cstr(ns_tok.start, ns_tok.length, "io") == VM_TRUE ||
                 vm_str_eq_cstr(ns_tok.start, ns_tok.length, "std.io") == VM_TRUE) &&
                (vm_str_eq_cstr(member.start, member.length, "trace") == VM_TRUE ||
                 vm_str_eq_cstr(member.start, member.length, "debug") == VM_TRUE)) {
                vm_u8 kind = (vm_str_eq_cstr(member.start, member.length, "trace") == VM_TRUE) ? 2 : 3;
                ExprResult msg = c_compile_expr(c, VM_TYPE_STRING);
                vm_u8 msg_reg = coerce_expr_to_string_reg(c, msg);
                vm_u8 level_reg;
                if (c_match(c, TOK_COMMA) == VM_TRUE) {
                    ExprResult lv = c_compile_expr(c, VM_TYPE_I32);
                    level_reg = lv.reg;
                } else {
                    VMReg one = {0};
                    one.i32 = 1;
                    level_reg = c_emit_load_const(c, one, VM_TYPE_I32);
                }
                c_consume(c, TOK_RPAREN, "Expected ')' after trace/debug arguments");
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_TRAP, kind, msg_reg, level_reg));
                return res;
            }
            if (ev >= 0 && c->enum_variants[ev].has_payload == VM_TRUE) {
                VMDataType ptype = c->enum_variants[ev].payload_type;
                ExprResult pay = c_compile_expr(c, ptype);
                c_consume(c, TOK_RPAREN, "Expected ')' after enum variant payload");
                vm_u8 base_reg = c_alloc_reg(c);
                vm_u8 pay_reg = c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, base_reg, (vm_i16)c->enum_variants[ev].tag));
                if (pay.reg != pay_reg) {
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, pay_reg, pay.reg, 0));
                }
                res.reg = base_reg;
                res.type = VM_TYPE_I32;
                res.elem_count = 2;
                res.elem_types[0] = VM_TYPE_I32;
                res.elem_types[1] = ptype;
                return res;
            }
            int sid = c_resolve_struct(c, ns_tok.start, ns_tok.length);
            return emit_overloaded_call(c, qname, qlen, -1, sid, member.start, member.length, VM_NULL);
        }

        if (ev >= 0 && c->enum_variants[ev].is_tagged_enum == VM_TRUE) {
            vm_u8 base_reg = c_alloc_reg(c);
            vm_u8 pay_reg = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, base_reg, (vm_i16)c->enum_variants[ev].tag));
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, pay_reg, 0));
            res.reg = base_reg;
            res.type = VM_TYPE_I32;
            res.elem_count = 2;
            res.elem_types[0] = VM_TYPE_I32;
            res.elem_types[1] = c->enum_variants[ev].payload_type;
            return res;
        }

        int gconst = c_resolve_global_const(c, qname, qlen);
        if (gconst >= 0) {
            res.type = c->global_consts[gconst].type;
            res.is_const = VM_TRUE;
            res.const_val = c->global_consts[gconst].val;
            res.reg = c_emit_load_const(c, res.const_val, res.type);
            return res;
        }

        int loc = c_resolve_local(c, ns_tok.start, ns_tok.length);
        if (loc >= 0 && c->locals[loc].struct_id >= 0) {
            int sid = c->locals[loc].struct_id;
            StructDef *sd = &c->structs[sid];
            for (vm_u8 f = 0; f < sd->num_fields; f++) {
                if (vm_str_eq_cstr(member.start, member.length, sd->field_names[f]) == VM_TRUE) {
                    if (c_can_access_struct_member(c, sid, sd->field_is_pub[f]) == VM_FALSE) {
                        c_error(c, "Cannot access private struct field (missing 'pub') from outside its struct");
                    }
                    res.reg = (vm_u8)(c->locals[loc].reg + f);
                    res.type = sd->field_types[f];
                    return res;
                }
            }
        }
        c_error(c, "Unknown member or field");
        return res;
    }

    if (c_match(c, TOK_LPAREN) == VM_TRUE) {
        if (vm_str_eq_cstr(id.start, id.length, "error") == VM_TRUE ||
            vm_str_eq_cstr(id.start, id.length, "warn") == VM_TRUE ||
            vm_str_eq_cstr(id.start, id.length, "trace") == VM_TRUE ||
            vm_str_eq_cstr(id.start, id.length, "debug") == VM_TRUE) {
            vm_u8 kind = (vm_str_eq_cstr(id.start, id.length, "warn") == VM_TRUE) ? 1 :
                         (vm_str_eq_cstr(id.start, id.length, "trace") == VM_TRUE) ? 2 :
                         (vm_str_eq_cstr(id.start, id.length, "debug") == VM_TRUE) ? 3 : 0;
            ExprResult msg = c_compile_expr(c, VM_TYPE_STRING);
            vm_u8 msg_reg = coerce_expr_to_string_reg(c, msg);
            vm_u8 level_reg;
            if (c_match(c, TOK_COMMA) == VM_TRUE) {
                ExprResult lv = c_compile_expr(c, VM_TYPE_I32);
                level_reg = lv.reg;
            } else {
                VMReg one = {0};
                one.i32 = 1;
                level_reg = c_emit_load_const(c, one, VM_TYPE_I32);
            }
            c_consume(c, TOK_RPAREN, "Expected ')' after error/warn/trace/debug arguments");
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_TRAP, kind, msg_reg, level_reg));
            return res;
        }
        if (vm_str_eq_cstr(id.start, id.length, "assert") == VM_TRUE) {
            ExprResult cond = c_compile_expr(c, VM_TYPE_BOOL);
            vm_u32 jmp_ok = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_TRUE, cond.reg, 0));
            vm_u8 msg_reg;
            if (c_match(c, TOK_COMMA) == VM_TRUE) {
                ExprResult msg = c_compile_expr(c, VM_TYPE_STRING);
                msg_reg = coerce_expr_to_string_reg(c, msg);
            } else {
                VMReg dmsg = {0};
                dmsg.str_id = vm_chunk_add_string(c->chunk, "Assertion failed", 16);
                msg_reg = c_emit_load_const(c, dmsg, VM_TYPE_STRING);
            }
            vm_u8 level_reg;
            if (c_match(c, TOK_COMMA) == VM_TRUE) {
                ExprResult lv = c_compile_expr(c, VM_TYPE_I32);
                level_reg = lv.reg;
            } else {
                VMReg one = {0};
                one.i32 = 1;
                level_reg = c_emit_load_const(c, one, VM_TYPE_I32);
            }
            c_consume(c, TOK_RPAREN, "Expected ')' after assert arguments");
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_TRAP, 0, msg_reg, level_reg));
            vm_i16 off = (vm_i16)(c->chunk->code_count - (jmp_ok + 1));
            c->chunk->code[jmp_ok] = VM_MAKE_ABx(OP_JMP_TRUE, cond.reg, off);
            return res;
        }
        if (vm_str_eq_cstr(id.start, id.length, "pcall") == VM_TRUE ||
            vm_str_eq_cstr(id.start, id.length, "xpcall") == VM_TRUE) {
            vm_bool is_xpcall = vm_str_eq_cstr(id.start, id.length, "xpcall");
            ExprResult fn_expr = c_compile_expr(c, VM_TYPE_I32);
            ExprResult handler_expr = {0};
            handler_expr.lambda_func_id = -1;
            if (is_xpcall == VM_TRUE) {
                c_consume(c, TOK_COMMA, "Expected ',' before error handler in xpcall");
                handler_expr = c_compile_expr(c, VM_TYPE_I32);
            }
            ExprResult cargs[8];
            vm_u8 argc = 0;
            while (c_match(c, TOK_COMMA) == VM_TRUE) {
                VMDataType exp_t = VM_TYPE_I32;
                if (fn_expr.lambda_func_id >= 0 && argc < c->chunk->functions[fn_expr.lambda_func_id].arity) {
                    exp_t = c->chunk->functions[fn_expr.lambda_func_id].param_types[argc];
                }
                cargs[argc++] = c_compile_expr(c, exp_t);
            }
            c_consume(c, TOK_RPAREN, "Expected ')' after pcall/xpcall arguments");

            VMDataType callee_ret = VM_TYPE_STRING;
            if (fn_expr.lambda_func_id >= 0) {
                VMDataType r = c->chunk->functions[fn_expr.lambda_func_id].return_type;
                if (r != VM_TYPE_VOID) callee_ret = r;
            }

            vm_u8 ok_reg = c_alloc_reg(c);
            vm_u8 val_reg = c_alloc_reg(c);
            vm_u32 try_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_TRY_BEGIN, val_reg, 0));

            vm_u8 call_base = c_alloc_reg(c);
            vm_u8 pcursor = (vm_u8)(call_base + 1);
            for (vm_u8 i = 0; i < argc; i++) {
                c_alloc_reg(c);
                if (cargs[i].reg != pcursor) {
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, pcursor, cargs[i].reg, 0));
                }
                pcursor++;
            }
            c->next_reg = (vm_u8)(call_base + 1);
            if (fn_expr.lambda_func_id >= 0) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, fn_expr.lambda_func_id));
            } else {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CALL_REG, call_base, fn_expr.reg, 0));
            }

            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_TRY_END, 0, 0, 0));
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LOAD_BOOL, ok_reg, 1, 0));
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, val_reg, call_base, 0));
            vm_u32 end_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, 0));

            vm_i16 catch_off = (vm_i16)(c->chunk->code_count - (try_jmp + 1));
            c->chunk->code[try_jmp] = VM_MAKE_ABx(OP_TRY_BEGIN, val_reg, catch_off);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LOAD_BOOL, ok_reg, 0, 0));

            if (is_xpcall == VM_TRUE) {
                vm_u8 hbase = c_alloc_reg(c);
                vm_u8 harg = c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, harg, val_reg, 0));
                c->next_reg = (vm_u8)(hbase + 1);
                if (handler_expr.lambda_func_id >= 0) {
                    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, hbase, handler_expr.lambda_func_id));
                } else {
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CALL_REG, hbase, handler_expr.reg, 0));
                }
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, val_reg, hbase, 0));
            }

            vm_i16 end_off = (vm_i16)(c->chunk->code_count - (end_jmp + 1));
            c->chunk->code[end_jmp] = VM_MAKE_ABx(OP_JMP, 0, end_off);
            c->next_reg = (vm_u8)(val_reg + 1);

            res.reg = ok_reg;
            res.type = VM_TYPE_BOOL;
            res.elem_count = 2;
            res.elem_types[0] = VM_TYPE_BOOL;
            res.elem_types[1] = callee_ret;
            return res;
        }

        int loc = c_resolve_local(c, id.start, id.length);
        if (loc >= 0 && c->locals[loc].lambda_func_id >= 0) {
            return emit_overloaded_call(c, "", 0, c->locals[loc].lambda_func_id, -1, "", 0, VM_NULL);
        }
        if (loc >= 0 && c->locals[loc].func_type_id >= 0) {
            TypeContract *ftc = &c->type_contracts[c->locals[loc].func_type_id];
            ExprResult fargs[8];
            vm_u8 argc = 0;
            if (c_check(c, TOK_RPAREN) == VM_FALSE) {
                do {
                    VMDataType exp_t = (argc < ftc->func_arity) ? ftc->func_param_types[argc] : VM_TYPE_I32;
                    fargs[argc] = c_compile_expr(c, exp_t);
                    argc++;
                } while (c_match(c, TOK_COMMA) == VM_TRUE);
            }
            c_consume(c, TOK_RPAREN, "Expected ')' after callback arguments");
            vm_u8 ret_slots = (ftc->func_ret_sid >= 0) ? c->structs[ftc->func_ret_sid].num_fields : 1;
            vm_u8 call_base = c_alloc_reg(c);
            for (vm_u8 r = 1; r < ret_slots; r++) c_alloc_reg(c);
            vm_u8 pcursor = (vm_u8)(call_base + ret_slots);
            for (vm_u8 i = 0; i < argc; i++) {
                vm_u8 cnt = (fargs[i].struct_id >= 0) ? c->structs[fargs[i].struct_id].num_fields : 1;
                for (vm_u8 k = 0; k < cnt; k++) {
                    c_alloc_reg(c);
                    if ((vm_u8)(fargs[i].reg + k) != pcursor) {
                        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, pcursor, (vm_u8)(fargs[i].reg + k), 0));
                    }
                    pcursor++;
                }
            }
            c->next_reg = (vm_u8)(call_base + ret_slots);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CALL_REG, call_base, c->locals[loc].reg, 0));
            res.reg = call_base;
            res.type = ftc->func_ret_type;
            res.struct_id = ftc->func_ret_sid;
            res.elem_count = ret_slots;
            return res;
        }
        if (loc >= 0 && c->locals[loc].struct_id >= 0) {
            ExprResult self_arg = {0};
            self_arg.reg = c->locals[loc].reg;
            self_arg.type = c->locals[loc].type;
            self_arg.struct_id = c->locals[loc].struct_id;
            self_arg.elem_count = c->locals[loc].elem_count;
            return emit_overloaded_call(c, "", 0, -1, self_arg.struct_id, "op_call", 7, &self_arg);
        }
        int oloc = c_resolve_outer_local(c, id.start, id.length);
        if (oloc >= 0 && c->outer_locals[oloc].lambda_func_id >= 0) {
            return emit_overloaded_call(c, "", 0, c->outer_locals[oloc].lambda_func_id, -1, "", 0, VM_NULL);
        }
        char qname[64];
        vm_str_copy(qname, id.start, id.length, 64);
        return emit_overloaded_call(c, qname, id.length, -1, -1, "", 0, VM_NULL);
    }

    int loc = c_resolve_local(c, id.start, id.length);
    if (loc >= 0) {
        res.reg = c->locals[loc].reg;
        res.type = c->locals[loc].type;
        res.is_const = c->locals[loc].is_const;
        res.const_val = c->locals[loc].const_val;
        res.elem_count = c->locals[loc].elem_count;
        for (vm_u8 k = 0; k < res.elem_count; k++) res.elem_types[k] = c->locals[loc].elem_types[k];
        res.struct_id = c->locals[loc].struct_id;
        res.lambda_func_id = c->locals[loc].lambda_func_id;
        res.lambda_gen_id = c->locals[loc].lambda_gen_id;
        return res;
    }

    /* Check outer enclosing function locals (Closure / Upvalue capture via OP_GET_OUTER) */
    int oloc = c_resolve_outer_local(c, id.start, id.length);
    if (oloc >= 0) {
        LocalVar *ov = &c->outer_locals[oloc];
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_GET_OUTER, dst, ov->reg, 1));
        for (vm_u8 k = 1; k < ov->elem_count; k++) {
            vm_u8 edst = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_GET_OUTER, edst, (vm_u8)(ov->reg + k), 1));
        }
        res.reg = dst;
        res.type = ov->type;
        res.elem_count = ov->elem_count;
        for (vm_u8 k = 0; k < res.elem_count; k++) res.elem_types[k] = ov->elem_types[k];
        res.struct_id = ov->struct_id;
        return res;
    }

    int gconst = c_resolve_global_const(c, id.start, id.length);
    if (gconst >= 0) {
        res.type = c->global_consts[gconst].type;
        res.is_const = VM_TRUE;
        res.const_val = c->global_consts[gconst].val;
        res.reg = c_emit_load_const(c, res.const_val, res.type);
        return res;
    }

    int fn_ref = c_resolve_func(c, id.start, id.length);
    if (fn_ref >= 0) {
        res.reg = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, res.reg, (vm_i16)fn_ref));
        res.type = VM_TYPE_I32;
        res.lambda_func_id = fn_ref;
        return res;
    }

    c_error(c, "Undefined variable or identifier");
    return res;
}
