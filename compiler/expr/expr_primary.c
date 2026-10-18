#include "expr.h"

static void append_fstring_reg(Compiler *c, vm_u8 part_reg, vm_bool *has_acc, vm_u8 *acc_reg) {
    if (*has_acc == VM_FALSE) {
        *acc_reg = part_reg;
        *has_acc = VM_TRUE;
    } else {
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRCAT, dst, *acc_reg, part_reg));
        *acc_reg = dst;
    }
}

static void flush_fstring_lit(Compiler *c, const char *buf, vm_u32 len, vm_bool *has_acc, vm_u8 *acc_reg) {
    if (len == 0) return;
    char *dst = vm_chunk_arena_reserve(c->chunk, len);
    if (dst == VM_NULL) return;
    for (vm_u32 i = 0; i < len; i++) dst[i] = buf[i];
    dst[len] = '\0';
    c->chunk->string_arena_used += len + 1;
    VMReg kval = {0};
    kval.str_id = vm_chunk_add_string(c->chunk, dst, len);
    vm_u8 sreg = c_emit_load_const(c, kval, VM_TYPE_STRING);
    append_fstring_reg(c, sreg, has_acc, acc_reg);
}

static ExprResult compile_fstring(Compiler *c, Token ftok) {
    ExprResult res = {0, VM_TYPE_STRING, VM_FALSE, {0}, 1, {VM_TYPE_STRING}, -1, -1, -1, VM_FALSE, VM_FALSE};
    const char *src = ftok.start;
    vm_u32 len = ftok.length;
    char seg[512];
    vm_u32 slen = 0;
    vm_bool has_acc = VM_FALSE;
    vm_u8 acc_reg = 0;

    vm_u32 i = 0;
    while (i < len) {
        if (src[i] == '\\' && i + 1 < len) {
            seg[slen++] = src[i + 1];
            i += 2;
            continue;
        }
        /* `{{` -> literal `{` (and `{{{expr}}}` -> literal `{` followed by `{expr}`) */
        if (src[i] == '{' && i + 1 < len && src[i + 1] == '{') {
            seg[slen++] = '{';
            i += 2;
            continue;
        }
        /* `}}` -> literal `}` */
        if (src[i] == '}' && i + 1 < len && src[i + 1] == '}') {
            seg[slen++] = '}';
            i += 2;
            continue;
        }
        if (src[i] == '{') {
            flush_fstring_lit(c, seg, slen, &has_acc, &acc_reg);
            slen = 0;
            i++; /* skip '{' */
            vm_u32 expr_start = i;
            int depth = 1;
            while (i < len && depth > 0) {
                if (src[i] == '{') depth++;
                else if (src[i] == '}') {
                    depth--;
                    if (depth == 0) break;
                }
                i++;
            }
            vm_u32 expr_len = i - expr_start;
            if (i < len && src[i] == '}') i++;

            char expr_buf[256];
            vm_str_copy(expr_buf, src + expr_start, expr_len, 256);

            Lexer saved_lex = c->lex;
            Token saved_cur = c->cur;
            lexer_init(&c->lex, expr_buf);
            c->lex.line = ftok.line;
            c_advance(c);
            ExprResult sub = c_compile_expr(c, VM_TYPE_STRING);
            c->lex = saved_lex;
            c->cur = saved_cur;

            vm_u8 str_reg = sub.reg;
            if (sub.struct_id >= 0) {
                int str_fid = c_resolve_struct_method(c, sub.struct_id, "op_str", 6);
                if (str_fid >= 0) {
                    int sid = sub.struct_id;
                    vm_u8 call_base = c_alloc_reg(c);
                    vm_u8 cursor = (vm_u8)(call_base + 1);
                    vm_u8 nf = c->structs[sid].num_fields;
                    for (vm_u8 f = 0; f < nf; f++) {
                        c_alloc_reg(c);
                        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(sub.reg + f), 0));
                    }
                    c->next_reg = (vm_u8)(call_base + 1);
                    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, str_fid));
                    str_reg = call_base;
                }
            } else if (sub.type != VM_TYPE_STRING) {
                str_reg = c_alloc_reg(c);
                vm_u8 tpair = (vm_u8)(((vm_u8)sub.type << 4) | ((vm_u8)VM_TYPE_STRING & 0x0Fu));
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CAST, str_reg, sub.reg, tpair));
            }
            append_fstring_reg(c, str_reg, &has_acc, &acc_reg);
            continue;
        }
        seg[slen++] = src[i++];
    }
    flush_fstring_lit(c, seg, slen, &has_acc, &acc_reg);
    if (has_acc == VM_FALSE) {
        VMReg empty_val = {0};
        empty_val.str_id = vm_chunk_add_string(c->chunk, "", 0);
        acc_reg = c_emit_load_const(c, empty_val, VM_TYPE_STRING);
    }
    res.reg = acc_reg;
    return res;
}

static ExprResult emit_struct_unary_op(Compiler *c, ExprResult inner, const char *op_name, vm_u32 op_len, const char *err_msg) {
    ExprResult res = {0, VM_TYPE_I32, VM_FALSE, {0}, 1, {VM_TYPE_I32}, -1, -1, -1, VM_FALSE, VM_FALSE};
    VMDataType arg_types[1] = {inner.type};
    int arg_sids[1] = {inner.struct_id};
    int fid = c_resolve_struct_method_overload(c, inner.struct_id, op_name, op_len, arg_types, arg_sids, 1);
    if (fid < 0) {
        c_error(c, err_msg);
        return res;
    }
    VMFunctionInfo *fn = &c->chunk->functions[fid];
    vm_u8 ret_slots = (fn->return_reg_count > 0) ? fn->return_reg_count : 1;
    vm_u8 call_base = c_alloc_reg(c);
    for (vm_u8 r = 1; r < ret_slots; r++) c_alloc_reg(c);
    vm_u8 cursor = (vm_u8)(call_base + ret_slots);
    vm_u8 nf = c->structs[inner.struct_id].num_fields;
    for (vm_u8 f = 0; f < nf; f++) {
        c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(inner.reg + f), 0));
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
    } else {
        res.elem_types[0] = res.type;
    }
    return res;
}

static ExprResult compile_atom(Compiler *c, VMDataType expected_type) {
    ExprResult res = {0, VM_TYPE_I32, VM_FALSE, {0}, 1, {VM_TYPE_I32}, -1, -1, -1, VM_FALSE, VM_FALSE};

    if (c_is_lambda_start(c) == VM_TRUE) {
        return c_compile_lambda_expr(c);
    }

    if (c_match(c, TOK_LBRACKET) == VM_TRUE) {
        vm_u8 arr_reg = c_alloc_reg(c);
        VMDataType elem_type = VM_TYPE_I32;
        vm_u32 patch_ip = vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_NEW, arr_reg, (vm_u8)elem_type, 0));
        vm_bool first_elem = VM_TRUE;
        if (c_check(c, TOK_RBRACKET) == VM_FALSE) {
            do {
                if (c_check(c, TOK_RBRACKET) == VM_TRUE) break;
                ExprResult elem = c_compile_expr(c, first_elem == VM_TRUE ? VM_TYPE_I32 : elem_type);
                if (first_elem == VM_TRUE) {
                    elem_type = elem.type;
                    c->chunk->code[patch_ip] = VM_MAKE_ABC(OP_ARR_NEW, arr_reg, (vm_u8)elem_type, 0);
                    first_elem = VM_FALSE;
                }
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_PUSH, arr_reg, elem.reg, 0));
            } while (c_match(c, TOK_COMMA) == VM_TRUE);
        }
        c_consume(c, TOK_RBRACKET, "Expected ']' after array literal");
        if (first_elem == VM_TRUE) c->last_empty_array_ip = patch_ip;
        res.reg = arr_reg;
        res.type = VM_TYPE_ARRAY;
        res.elem_types[0] = elem_type;
        return res;
    }

    if (c_match(c, TOK_LBRACE) == VM_TRUE) {
        vm_u8 map_reg = c_alloc_reg(c);
        VMDataType val_type = VM_TYPE_I32;
        vm_u32 patch_ip = vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_NEW, map_reg, (vm_u8)VM_TYPE_STRING, (vm_u8)val_type));
        vm_bool first_entry = VM_TRUE;
        if (c_match(c, TOK_COLON) == VM_TRUE) {
            /* Empty map literal `{:}` */
        } else if (c_check(c, TOK_RBRACE) == VM_FALSE) {
            do {
                if (c_check(c, TOK_RBRACE) == VM_TRUE) break;
                ExprResult kexpr = c_compile_expr(c, VM_TYPE_STRING);
                c_consume(c, TOK_COLON, "Expected ':' after map key");
                ExprResult vexpr = c_compile_expr(c, first_entry == VM_TRUE ? VM_TYPE_I32 : val_type);
                if (first_entry == VM_TRUE) {
                    val_type = vexpr.type;
                    c->chunk->code[patch_ip] = VM_MAKE_ABC(OP_MAP_NEW, map_reg, (vm_u8)VM_TYPE_STRING, (vm_u8)val_type);
                    first_entry = VM_FALSE;
                }
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_SET, map_reg, kexpr.reg, vexpr.reg));
            } while (c_match(c, TOK_COMMA) == VM_TRUE);
        }
        c_consume(c, TOK_RBRACE, "Expected '}' after map literal");
        res.reg = map_reg;
        res.type = VM_TYPE_MAP;
        res.elem_types[0] = val_type;
        return res;
    }

    if (c_match(c, TOK_LPAREN) == VM_TRUE) {
        ExprResult first = c_compile_expr(c, expected_type);
        if (c_match(c, TOK_COMMA) == VM_TRUE) {
            vm_u8 base_reg = first.reg;
            res.reg = base_reg;
            res.type = first.type;
            res.elem_count = 1;
            res.elem_types[0] = first.type;
            do {
                vm_u8 slot = c_alloc_reg(c);
                vm_u8 save = c->next_reg;
                ExprResult elem = c_compile_expr(c, VM_TYPE_I32);
                c->next_reg = save;
                if (elem.reg != slot) {
                    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, slot, elem.reg, 0));
                }
                res.elem_types[res.elem_count++] = elem.type;
            } while (c_match(c, TOK_COMMA) == VM_TRUE);
            c_consume(c, TOK_RPAREN, "Expected ')' after tuple");
            return res;
        }
        c_consume(c, TOK_RPAREN, "Expected ')'");
        return first;
    }

    if (c_check(c, TOK_INT_LIT) == VM_TRUE) {
        vm_i64 v = c->cur.int_val;
        c_advance(c);
        res.is_const = VM_TRUE;
        if (expected_type == VM_TYPE_I64) { res.type = VM_TYPE_I64; res.const_val.i64 = v; }
        else if (expected_type == VM_TYPE_F32) { res.type = VM_TYPE_F32; res.const_val.f32 = (vm_f32)v; }
        else if (expected_type == VM_TYPE_F64) { res.type = VM_TYPE_F64; res.const_val.f64 = (vm_f64)v; }
        else { res.type = VM_TYPE_I32; res.const_val.i32 = (vm_i32)v; }
        res.reg = c_emit_load_const(c, res.const_val, res.type);
        return res;
    }

    if (c_check(c, TOK_FLOAT_LIT) == VM_TRUE) {
        vm_f64 v = c->cur.float_val;
        c_advance(c);
        res.is_const = VM_TRUE;
        if (expected_type == VM_TYPE_F64) { res.type = VM_TYPE_F64; res.const_val.f64 = v; }
        else { res.type = VM_TYPE_F32; res.const_val.f32 = (vm_f32)v; }
        res.reg = c_emit_load_const(c, res.const_val, res.type);
        return res;
    }

    if (c_check(c, TOK_TRUE) == VM_TRUE || c_check(c, TOK_FALSE) == VM_TRUE) {
        vm_bool b = vm_bool_from_int(c->cur.type == TOK_TRUE);
        c_advance(c);
        res.type = VM_TYPE_BOOL;
        res.is_const = VM_TRUE;
        res.const_val.b = b;
        res.reg = c_emit_load_const(c, res.const_val, VM_TYPE_BOOL);
        return res;
    }

    if (c_check(c, TOK_STRING_LIT) == VM_TRUE) {
        vm_u32 sid = vm_chunk_add_string(c->chunk, c->cur.start, c->cur.length);
        c_advance(c);
        res.type = VM_TYPE_STRING;
        res.elem_types[0] = VM_TYPE_STRING;
        res.is_const = VM_TRUE;
        res.const_val.str_id = sid;
        res.reg = c_emit_load_const(c, res.const_val, VM_TYPE_STRING);
        return res;
    }

    if (c_check(c, TOK_FSTRING_LIT) == VM_TRUE) {
        Token ftok = c->cur;
        c_advance(c);
        return compile_fstring(c, ftok);
    }

    if (c_match(c, TOK_ELLIPSIS) == VM_TRUE) {
        int loc = c_resolve_local(c, "...", 3);
        if (loc >= 0 && c_match(c, TOK_DOT) == VM_TRUE && c_check(c, TOK_INT_LIT) == VM_TRUE) {
            vm_u8 idx = (vm_u8)c->cur.int_val;
            c_advance(c);
            res.reg = (vm_u8)(c->locals[loc].reg + idx);
            res.type = c->locals[loc].elem_types[idx];
            return res;
        }
    }

    if (c_match(c, TOK_SELF_TYPE) == VM_TRUE) {
        if (c->current_struct_id < 0) {
            c_error(c, "'Self' can only be used inside a 'struct' or 'type'");
            return res;
        }
        Token self_tok = {TOK_IDENT, c->structs[c->current_struct_id].name,
                          vm_str_len(c->structs[c->current_struct_id].name), c->cur.line, 0, 0.0};
        return c_compile_ident_or_call(c, self_tok);
    }

    if (c_check(c, TOK_IDENT) == VM_TRUE || c_check(c, TOK_TYPE_STRING) == VM_TRUE) {
        Token id = c->cur;
        c_advance(c);
        return c_compile_ident_or_call(c, id);
    }

    c_error(c, "Expected expression");
    c_advance(c);
    return res;
}

ExprResult c_compile_primary(Compiler *c, VMDataType expected_type) {
    ExprResult res = {0, VM_TYPE_I32, VM_FALSE, {0}, 1, {VM_TYPE_I32}, -1, -1, -1, VM_FALSE, VM_FALSE};

    if (c_match(c, TOK_HASH) == VM_TRUE) {
        ExprResult arg = c_compile_primary(c, VM_TYPE_STRING);
        if (arg.struct_id >= 0) {
            return emit_struct_unary_op(c, arg, "op_len", 6,
                "'#' length operator requires a string, array, map, or struct defining 'operator #' / 'operator len'");
        }
        vm_u8 dst = c_alloc_reg(c);
        VMOpCode len_op = (arg.type == VM_TYPE_ARRAY) ? OP_ARR_LEN :
                          (arg.type == VM_TYPE_MAP)   ? OP_MAP_LEN : OP_STRLEN;
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(len_op, dst, arg.reg, 0));
        res.reg = dst;
        res.type = VM_TYPE_I32;
        return res;
    }

    if (c_match(c, TOK_LEN) == VM_TRUE) {
        c_consume(c, TOK_LPAREN, "Expected '(' after len");
        ExprResult arg = c_compile_expr(c, VM_TYPE_STRING);
        c_consume(c, TOK_RPAREN, "Expected ')'");
        if (arg.struct_id >= 0) {
            return emit_struct_unary_op(c, arg, "op_len", 6,
                "'len()' requires a string, array, map, or struct defining 'operator #' / 'operator len'");
        }
        vm_u8 dst = c_alloc_reg(c);
        VMOpCode len_op = (arg.type == VM_TYPE_ARRAY) ? OP_ARR_LEN :
                          (arg.type == VM_TYPE_MAP)   ? OP_MAP_LEN : OP_STRLEN;
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(len_op, dst, arg.reg, 0));
        res.reg = dst;
        res.type = VM_TYPE_I32;
        return res;
    }

    if (c_match(c, TOK_MINUS) == VM_TRUE) {
        ExprResult inner = c_compile_primary(c, expected_type);
        if (inner.struct_id >= 0) {
            return emit_struct_unary_op(c, inner, "op-", 3, "Struct does not define unary 'operator -'");
        }
        if (inner.is_const == VM_TRUE) {
            if (inner.type == VM_TYPE_I32) inner.const_val.i32 = -inner.const_val.i32;
            else if (inner.type == VM_TYPE_I64) inner.const_val.i64 = -inner.const_val.i64;
            else if (inner.type == VM_TYPE_F32) inner.const_val.f32 = -inner.const_val.f32;
            else if (inner.type == VM_TYPE_F64) inner.const_val.f64 = -inner.const_val.f64;
            inner.reg = c_emit_load_const(c, inner.const_val, inner.type);
            return inner;
        }
        VMReg zero = {0};
        vm_u8 zreg = c_emit_load_const(c, zero, inner.type);
        vm_u8 dst = c_alloc_reg(c);
        VMOpCode op = (inner.type == VM_TYPE_F32) ? OP_SUB_F32 :
                      (inner.type == VM_TYPE_F64) ? OP_SUB_F64 :
                      (inner.type == VM_TYPE_I64) ? OP_SUB_I64 : OP_SUB_I32;
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(op, dst, zreg, inner.reg));
        res.reg = dst;
        res.type = inner.type;
        return res;
    }

    if (c_match(c, TOK_BNOT) == VM_TRUE) {
        ExprResult inner = c_compile_primary(c, VM_TYPE_I32);
        if (inner.struct_id >= 0) {
            return emit_struct_unary_op(c, inner, "op~", 3, "Struct does not define unary 'operator ~'");
        }
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_BNOT_I32, dst, inner.reg, 0));
        res.reg = dst;
        res.type = VM_TYPE_I32;
        return res;
    }

    if (c_match(c, TOK_LNOT) == VM_TRUE) {
        ExprResult inner = c_compile_primary(c, VM_TYPE_BOOL);
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_NOT_BOOL, dst, inner.reg, 0));
        res.reg = dst;
        res.type = VM_TYPE_BOOL;
        return res;
    }

    res = compile_atom(c, expected_type);
    while (c_is_postfix_method_call(c) == VM_TRUE) {
        c_advance(c); /* consume ':' */
        res = c_compile_postfix_method(c, res);
    }
    return res;
}
