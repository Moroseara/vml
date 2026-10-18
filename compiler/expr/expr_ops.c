#include "expr.h"
#include "../stmt/stmt.h"

static ExprResult emit_struct_binop(Compiler *c, ExprResult left, ExprResult right, const char *op_name) {
    ExprResult res = {0, VM_TYPE_I32, VM_FALSE, {0}, 1, {VM_TYPE_I32}, -1, -1, -1, VM_FALSE, VM_FALSE};
    VMDataType arg_types[2] = {left.type, right.type};
    int arg_sids[2] = {left.struct_id, right.struct_id};
    int fid = c_resolve_struct_method_overload(c, left.struct_id, op_name, vm_str_len(op_name), arg_types, arg_sids, 2);
    if (fid < 0) {
        c_error(c, "Struct does not define this overloaded operator");
        return res;
    }
    VMFunctionInfo *fn = &c->chunk->functions[fid];
    vm_u8 ret_slots = (fn->return_reg_count > 0) ? fn->return_reg_count : 1;
    vm_u8 call_base = c_alloc_reg(c);
    for (vm_u8 r = 1; r < ret_slots; r++) c_alloc_reg(c);

    vm_u8 cursor = (vm_u8)(call_base + ret_slots);
    vm_u8 lcount = (left.struct_id >= 0) ? c->structs[left.struct_id].num_fields : 1;
    for (vm_u8 k = 0; k < lcount; k++) {
        c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(left.reg + k), 0));
    }
    vm_u8 rcount = (right.struct_id >= 0) ? c->structs[right.struct_id].num_fields : 1;
    for (vm_u8 k = 0; k < rcount; k++) {
        c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(right.reg + k), 0));
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

static vm_bool is_cast_target_tok(Compiler *c, Token tok) {
    if (tok.type >= TOK_TYPE_I32 && tok.type <= TOK_TYPE_STRING) return VM_TRUE;
    if (tok.type == TOK_IDENT) {
        if (c_resolve_struct(c, tok.start, tok.length) >= 0) return VM_TRUE;
        if (c_resolve_type_contract(c, tok.start, tok.length) >= 0) return VM_TRUE;
    }
    return VM_FALSE;
}

static ExprResult compile_cast_or_primary(Compiler *c, VMDataType expected_type) {
    ExprResult left = c_compile_primary(c, expected_type);
    for (;;) {
        if (c_is_postfix_method_call(c) == VM_TRUE) {
            c_advance(c); /* consume ':' */
            left = c_compile_postfix_method(c, left);
            continue;
        }
        if (c_check(c, TOK_AS) == VM_TRUE && is_cast_target_tok(c, lexer_peek(&c->lex)) == VM_TRUE) {
            c_advance(c); /* consume 'as' */
            int dst_sid = -1;
            VMDataType dst_type = c_parse_type_ex(c, &dst_sid);
            if (left.struct_id >= 0) {
                char as_name[48];
                if (dst_sid >= 0) {
                    const char *sname = c->structs[dst_sid].name;
                    vm_u32 slen = vm_str_len(sname);
                    vm_str_copy(as_name, "op_as_", 6, 48);
                    for (vm_u32 k = 0; k < slen && (6 + k) < 47; k++) as_name[6 + k] = sname[k];
                    as_name[6 + slen] = '\0';
                } else if (dst_type == VM_TYPE_STRING) {
                    vm_str_copy(as_name, "op_str", 6, 48);
                } else if (dst_type == VM_TYPE_I64) {
                    vm_str_copy(as_name, "op_as_i64", 9, 48);
                } else if (dst_type == VM_TYPE_F32) {
                    vm_str_copy(as_name, "op_as_f32", 9, 48);
                } else if (dst_type == VM_TYPE_F64) {
                    vm_str_copy(as_name, "op_as_f64", 9, 48);
                } else if (dst_type == VM_TYPE_BOOL) {
                    vm_str_copy(as_name, "op_as_bool", 10, 48);
                } else {
                    vm_str_copy(as_name, "op_as_i32", 9, 48);
                }
                int as_fid = c_resolve_struct_method(c, left.struct_id, as_name, vm_str_len(as_name));
                if (as_fid >= 0) {
                    int sid = left.struct_id;
                    VMFunctionInfo *fn = &c->chunk->functions[as_fid];
                    vm_u8 ret_slots = (fn->return_reg_count > 0) ? fn->return_reg_count : 1;
                    vm_u8 call_base = c_alloc_reg(c);
                    for (vm_u8 r = 1; r < ret_slots; r++) c_alloc_reg(c);
                    vm_u8 cursor = (vm_u8)(call_base + ret_slots);
                    vm_u8 nf = c->structs[sid].num_fields;
                    for (vm_u8 f = 0; f < nf; f++) {
                        c_alloc_reg(c);
                        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(left.reg + f), 0));
                    }
                    c->next_reg = (vm_u8)(call_base + ret_slots);
                    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, as_fid));
                    left.reg = call_base;
                    left.type = fn->return_type;
                    left.struct_id = fn->return_struct_id;
                    left.elem_count = ret_slots;
                    left.is_const = VM_FALSE;
                    continue;
                }
                c_error(c, "Struct does not define matching 'operator as' for target type");
                continue;
            }
            vm_u8 dst = c_alloc_reg(c);
            vm_u8 type_pair = (vm_u8)(((vm_u8)left.type << 4) | ((vm_u8)dst_type & 0x0Fu));
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CAST, dst, left.reg, type_pair));
            left.reg = dst;
            left.type = dst_type;
            left.is_const = VM_FALSE;
            continue;
        }
        break;
    }
    return left;
}

static ExprResult compile_mul(Compiler *c, VMDataType expected_type) {
    ExprResult left = compile_cast_or_primary(c, expected_type);
    while (c_check(c, TOK_STAR) == VM_TRUE || c_check(c, TOK_SLASH) == VM_TRUE || c_check(c, TOK_PERCENT) == VM_TRUE) {
        TokenType op_tok = c->cur.type;
        c_advance(c);
        ExprResult right = compile_cast_or_primary(c, left.type);
        if (left.struct_id >= 0) {
            const char *op_name = (op_tok == TOK_STAR) ? "op*" : (op_tok == TOK_SLASH) ? "op/" : "op%";
            left = emit_struct_binop(c, left, right, op_name);
            continue;
        }
        vm_u8 dst = c_alloc_reg(c);
        VMOpCode op;
        if (op_tok == TOK_STAR) {
            op = (left.type == VM_TYPE_F32) ? OP_MUL_F32 :
                 (left.type == VM_TYPE_F64) ? OP_MUL_F64 :
                 (left.type == VM_TYPE_I64) ? OP_MUL_I64 : OP_MUL_I32;
        } else if (op_tok == TOK_SLASH) {
            op = (left.type == VM_TYPE_F32) ? OP_DIV_F32 :
                 (left.type == VM_TYPE_F64) ? OP_DIV_F64 :
                 (left.type == VM_TYPE_I64) ? OP_DIV_I64 : OP_DIV_I32;
        } else {
            op = OP_MOD_I32;
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(op, dst, left.reg, right.reg));
        left.reg = dst;
        left.is_const = VM_FALSE;
    }
    return left;
}

static ExprResult compile_add(Compiler *c, VMDataType expected_type) {
    ExprResult left = compile_mul(c, expected_type);
    while (c_check(c, TOK_PLUS) == VM_TRUE || c_check(c, TOK_MINUS) == VM_TRUE) {
        TokenType op_tok = c->cur.type;
        c_advance(c);
        vm_u32 before_cnt = c->chunk->code_count;
        vm_u8 before_reg = c->next_reg;
        ExprResult right = compile_mul(c, left.type);

        if (left.struct_id >= 0) {
            left = emit_struct_binop(c, left, right, (op_tok == TOK_PLUS) ? "op+" : "op-");
            continue;
        }

        if (op_tok == TOK_PLUS && left.type == VM_TYPE_STRING) {
            vm_u8 dst = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRCAT, dst, left.reg, right.reg));
            left.reg = dst;
            left.type = VM_TYPE_STRING;
            left.is_const = VM_FALSE;
            continue;
        }

        if (left.is_const == VM_FALSE && right.is_const == VM_TRUE && left.type == VM_TYPE_I32 &&
            right.const_val.i32 >= -127 && right.const_val.i32 <= 127) {
            c->chunk->code_count = before_cnt;
            c->next_reg = before_reg;
            vm_i8 imm = (op_tok == TOK_PLUS) ? (vm_i8)right.const_val.i32 : (vm_i8)(-right.const_val.i32);
            vm_u8 dst = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ADD_IMM_I32, dst, left.reg, (vm_u8)imm));
            left.reg = dst;
            continue;
        }

        vm_u8 dst = c_alloc_reg(c);
        VMOpCode op = (op_tok == TOK_PLUS)
                      ? ((left.type == VM_TYPE_F32) ? OP_ADD_F32 :
                         (left.type == VM_TYPE_F64) ? OP_ADD_F64 :
                         (left.type == VM_TYPE_I64) ? OP_ADD_I64 : OP_ADD_I32)
                      : ((left.type == VM_TYPE_F32) ? OP_SUB_F32 :
                         (left.type == VM_TYPE_F64) ? OP_SUB_F64 :
                         (left.type == VM_TYPE_I64) ? OP_SUB_I64 : OP_SUB_I32);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(op, dst, left.reg, right.reg));
        left.reg = dst;
        left.is_const = VM_FALSE;
    }
    return left;
}

static ExprResult compile_shift_bit(Compiler *c, VMDataType expected_type) {
    ExprResult left = compile_add(c, expected_type);
    while (c_check(c, TOK_SHL) == VM_TRUE || c_check(c, TOK_SHR) == VM_TRUE ||
           c_check(c, TOK_BAND) == VM_TRUE || c_check(c, TOK_BXOR) == VM_TRUE || c_check(c, TOK_BOR) == VM_TRUE) {
        TokenType op_tok = c->cur.type;
        c_advance(c);
        ExprResult right = compile_add(c, VM_TYPE_I32);
        if (left.struct_id >= 0) {
            const char *op_name = (op_tok == TOK_SHL) ? "op<<" :
                                  (op_tok == TOK_SHR) ? "op>>" :
                                  (op_tok == TOK_BAND) ? "op&" :
                                  (op_tok == TOK_BXOR) ? "op^" : "op|";
            left = emit_struct_binop(c, left, right, op_name);
            continue;
        }
        if (left.is_const == VM_TRUE && right.is_const == VM_TRUE && left.type == VM_TYPE_I32) {
            if (op_tok == TOK_SHL) left.const_val.i32 <<= (right.const_val.i32 & 31);
            else if (op_tok == TOK_SHR) left.const_val.i32 >>= (right.const_val.i32 & 31);
            else if (op_tok == TOK_BAND) left.const_val.i32 &= right.const_val.i32;
            else if (op_tok == TOK_BXOR) left.const_val.i32 ^= right.const_val.i32;
            else left.const_val.i32 |= right.const_val.i32;
            left.reg = c_emit_load_const(c, left.const_val, VM_TYPE_I32);
            continue;
        }
        vm_u8 dst = c_alloc_reg(c);
        VMOpCode op = (op_tok == TOK_SHL) ? OP_SHL_I32 : (op_tok == TOK_SHR) ? OP_SHR_I32 :
                      (op_tok == TOK_BAND) ? OP_BAND_I32 : (op_tok == TOK_BXOR) ? OP_BXOR_I32 : OP_BOR_I32;
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(op, dst, left.reg, right.reg));
        left.reg = dst;
        left.is_const = VM_FALSE;
    }
    return left;
}

static ExprResult compile_cmp(Compiler *c, VMDataType expected_type) {
    ExprResult left = compile_shift_bit(c, expected_type);
    if (c_match(c, TOK_IN) == VM_TRUE) {
        ExprResult right = compile_shift_bit(c, VM_TYPE_STRING);
        if (right.struct_id >= 0) {
            return emit_struct_binop(c, right, left, "op_in");
        }
        if (right.type == VM_TYPE_MAP && left.type == VM_TYPE_STRING) {
            vm_u8 dst = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_HAS, dst, right.reg, left.reg));
            left.reg = dst;
            left.type = VM_TYPE_BOOL;
            left.struct_id = -1;
            left.elem_count = 1;
            left.elem_types[0] = VM_TYPE_BOOL;
            left.is_const = VM_FALSE;
            return left;
        }
        if (right.type == VM_TYPE_ARRAY) {
            int arr_sid = c_resolve_struct(c, "Array", 5);
            VMDataType at[2] = {VM_TYPE_ARRAY, left.type};
            int as[2] = {-1, -1};
            int fid = c_resolve_struct_method_overload(c, arr_sid, "contains", 8, at, as, 2);
            if (fid >= 0) {
                vm_u8 call_base = c_alloc_reg(c);
                vm_u8 r1 = c_alloc_reg(c);
                vm_u8 r2 = c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, r1, right.reg, 0));
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, r2, left.reg, 0));
                c->next_reg = (vm_u8)(call_base + 1);
                vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, fid));
                left.reg = call_base;
                left.type = VM_TYPE_BOOL;
                left.struct_id = -1;
                left.elem_count = 1;
                left.elem_types[0] = VM_TYPE_BOOL;
                left.is_const = VM_FALSE;
                return left;
            }
        }
        if (right.type == VM_TYPE_STRING && left.type == VM_TYPE_STRING) {
            vm_u8 idx_reg = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRFIND, idx_reg, right.reg, left.reg));
            VMReg zero = {0};
            vm_u8 zreg = c_emit_load_const(c, zero, VM_TYPE_I32);
            vm_u8 dst = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_GE_I32, dst, idx_reg, zreg));
            left.reg = dst;
            left.type = VM_TYPE_BOOL;
            left.struct_id = -1;
            left.elem_count = 1;
            left.elem_types[0] = VM_TYPE_BOOL;
            left.is_const = VM_FALSE;
            return left;
        }
        c_error(c, "'in' operator requires a string, array, map, or struct defining 'operator in'");
        return left;
    }
    if (c_match(c, TOK_IS) == VM_TRUE) {
        int is_fid = (left.struct_id >= 0) ? c_resolve_struct_method(c, left.struct_id, "op_is", 5) : -1;
        if (is_fid >= 0 && c_check(c, TOK_STRING_LIT) == VM_TRUE) {
            ExprResult tag_str = c_compile_primary(c, VM_TYPE_STRING);
            return emit_struct_binop(c, left, tag_str, "op_is");
        }
        vm_bool is_match = VM_FALSE;
        Token target_tok = c->cur;
        int tid = (c_check(c, TOK_IDENT) == VM_TRUE) ? c_resolve_type_contract(c, c->cur.start, c->cur.length) : -1;
        if (tid >= 0 && c->type_contracts[tid].is_alias == VM_FALSE) {
            c_advance(c);
            is_match = (left.struct_id >= 0) ? c_does_struct_implement_type(c, left.struct_id, tid) : VM_FALSE;
        } else if (is_fid >= 0 && c_check(c, TOK_IDENT) == VM_TRUE &&
                   c_resolve_struct(c, c->cur.start, c->cur.length) < 0) {
            c_advance(c);
            VMFunctionInfo *fn = &c->chunk->functions[is_fid];
            if (fn->arity == 1) {
                VMDataType at[1] = {left.type};
                int as[1] = {left.struct_id};
                int f1 = c_resolve_struct_method_overload(c, left.struct_id, "op_is", 5, at, as, 1);
                if (f1 >= 0) {
                    vm_u8 call_base = c_alloc_reg(c);
                    vm_u8 cursor = (vm_u8)(call_base + 1);
                    vm_u8 nf = c->structs[left.struct_id].num_fields;
                    for (vm_u8 f = 0; f < nf; f++) {
                        c_alloc_reg(c);
                        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor++, (vm_u8)(left.reg + f), 0));
                    }
                    c->next_reg = (vm_u8)(call_base + 1);
                    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, f1));
                    left.reg = call_base;
                    left.type = VM_TYPE_BOOL;
                    left.struct_id = -1;
                    left.elem_count = 1;
                    left.elem_types[0] = VM_TYPE_BOOL;
                    left.is_const = VM_FALSE;
                    return left;
                }
            }
            VMReg kval = {0};
            kval.str_id = vm_chunk_add_string(c->chunk, target_tok.start, target_tok.length);
            ExprResult tag_arg = {0, VM_TYPE_STRING, VM_TRUE, kval, 1, {VM_TYPE_STRING}, -1, -1, -1, VM_FALSE, VM_FALSE};
            tag_arg.reg = c_emit_load_const(c, kval, VM_TYPE_STRING);
            return emit_struct_binop(c, left, tag_arg, "op_is");
        } else {
            int check_sid = -1;
            VMDataType check_type = c_parse_type_ex(c, &check_sid);
            if (check_sid >= 0) {
                is_match = c_is_struct_compatible(c, left.struct_id, check_sid);
            } else {
                is_match = vm_bool_from_int(left.type == check_type);
            }
        }
        if (is_match == VM_FALSE && is_fid >= 0) {
            VMReg kval = {0};
            kval.str_id = vm_chunk_add_string(c->chunk, target_tok.start, target_tok.length);
            ExprResult tag_arg = {0, VM_TYPE_STRING, VM_TRUE, kval, 1, {VM_TYPE_STRING}, -1, -1, -1, VM_FALSE, VM_FALSE};
            tag_arg.reg = c_emit_load_const(c, kval, VM_TYPE_STRING);
            return emit_struct_binop(c, left, tag_arg, "op_is");
        }
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LOAD_BOOL, dst, (vm_u8)is_match, 0));
        left.reg = dst;
        left.type = VM_TYPE_BOOL;
        left.struct_id = -1;
        left.elem_count = 1;
        left.elem_types[0] = VM_TYPE_BOOL;
        left.is_const = VM_TRUE;
        left.const_val.b = is_match;
        return left;
    }
    if (c_check(c, TOK_EQ) == VM_TRUE || c_check(c, TOK_NE) == VM_TRUE ||
        c_check(c, TOK_LT) == VM_TRUE || c_check(c, TOK_LE) == VM_TRUE ||
        c_check(c, TOK_GT) == VM_TRUE || c_check(c, TOK_GE) == VM_TRUE) {
        TokenType op_tok = c->cur.type;
        c_advance(c);
        ExprResult right = compile_shift_bit(c, left.type);
        if (left.struct_id >= 0) {
            const char *op_name = (op_tok == TOK_EQ) ? "op==" :
                                  (op_tok == TOK_NE) ? "op!=" :
                                  (op_tok == TOK_LT) ? "op<"  :
                                  (op_tok == TOK_LE) ? "op<=" :
                                  (op_tok == TOK_GT) ? "op>"  : "op>=";
            VMDataType arg_types[2] = {left.type, right.type};
            int arg_sids[2] = {left.struct_id, right.struct_id};
            int fid = c_resolve_struct_method_overload(c, left.struct_id, op_name, vm_str_len(op_name), arg_types, arg_sids, 2);
            if (fid >= 0) {
                return emit_struct_binop(c, left, right, op_name);
            }
            /* Lua-style automatic fallback: `a != b` -> `!(a == b)` */
            if (op_tok == TOK_NE) {
                ExprResult eq_res = emit_struct_binop(c, left, right, "op==");
                vm_u8 dst = c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_NOT_BOOL, dst, eq_res.reg, 0));
                eq_res.reg = dst;
                return eq_res;
            }
            /* `a > b` -> `b < a` */
            if (op_tok == TOK_GT && right.struct_id >= 0) {
                return emit_struct_binop(c, right, left, "op<");
            }
            /* `a <= b` -> `!(b < a)` */
            if (op_tok == TOK_LE && right.struct_id >= 0) {
                ExprResult lt_res = emit_struct_binop(c, right, left, "op<");
                vm_u8 dst = c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_NOT_BOOL, dst, lt_res.reg, 0));
                lt_res.reg = dst;
                return lt_res;
            }
            /* `a >= b` -> `!(a < b)` */
            if (op_tok == TOK_GE) {
                ExprResult lt_res = emit_struct_binop(c, left, right, "op<");
                vm_u8 dst = c_alloc_reg(c);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_NOT_BOOL, dst, lt_res.reg, 0));
                lt_res.reg = dst;
                return lt_res;
            }
            return emit_struct_binop(c, left, right, op_name);
        }
        vm_u8 dst = c_alloc_reg(c);
        VMOpCode op;
        if (left.type == VM_TYPE_STRING) {
            op = (op_tok == TOK_EQ) ? OP_EQ_STR : (op_tok == TOK_NE) ? OP_NE_STR :
                 (op_tok == TOK_LT) ? OP_LT_STR : (op_tok == TOK_LE) ? OP_LE_STR :
                 (op_tok == TOK_GT) ? OP_GT_STR : OP_GE_STR;
        } else if (left.type == VM_TYPE_F32) {
            op = (op_tok == TOK_EQ) ? OP_EQ_F32 : (op_tok == TOK_NE) ? OP_NE_F32 :
                 (op_tok == TOK_LT) ? OP_LT_F32 : (op_tok == TOK_LE) ? OP_LE_F32 :
                 (op_tok == TOK_GT) ? OP_GT_F32 : OP_GE_F32;
        } else if (left.type == VM_TYPE_F64) {
            op = (op_tok == TOK_EQ) ? OP_EQ_F64 : (op_tok == TOK_NE) ? OP_NE_F64 :
                 (op_tok == TOK_LT) ? OP_LT_F64 : (op_tok == TOK_LE) ? OP_LE_F64 :
                 (op_tok == TOK_GT) ? OP_GT_F64 : OP_GE_F64;
        } else if (left.type == VM_TYPE_I64) {
            op = (op_tok == TOK_EQ) ? OP_EQ_I64 : (op_tok == TOK_NE) ? OP_NE_I64 :
                 (op_tok == TOK_LT) ? OP_LT_I64 : (op_tok == TOK_LE) ? OP_LE_I64 :
                 (op_tok == TOK_GT) ? OP_GT_I64 : OP_GE_I64;
        } else {
            op = (op_tok == TOK_EQ) ? OP_EQ_I32 : (op_tok == TOK_NE) ? OP_NE_I32 :
                 (op_tok == TOK_LT) ? OP_LT_I32 : (op_tok == TOK_LE) ? OP_LE_I32 :
                 (op_tok == TOK_GT) ? OP_GT_I32 : OP_GE_I32;
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(op, dst, left.reg, right.reg));
        left.reg = dst;
        left.type = VM_TYPE_BOOL;
        left.is_const = VM_FALSE;
    }
    return left;
}

ExprResult c_compile_expr(Compiler *c, VMDataType expected_type) {
    if (c_check(c, TOK_IDENT) == VM_TRUE &&
        vm_str_eq_cstr(c->cur.start, c->cur.length, "try") == VM_TRUE) {
        Token next = lexer_peek(&c->lex);
        if (next.type != TOK_LPAREN && next.type != TOK_DOT && next.type != TOK_ASSIGN) {
            c_advance(c); /* consume 'try' */
            vm_u8 dst = c_alloc_reg(c);
            vm_u8 err_reg = c_alloc_reg(c);
            vm_u32 try_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_TRY_BEGIN, err_reg, 0));

            ExprResult body = compile_cmp(c, expected_type);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_TRY_END, 0, 0, 0));
            if (body.reg != dst) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, body.reg, 0));
            }
            vm_u32 end_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, 0));

            vm_i16 catch_off = (vm_i16)(c->chunk->code_count - (try_jmp + 1));
            c->chunk->code[try_jmp] = VM_MAKE_ABx(OP_TRY_BEGIN, err_reg, catch_off);

            VMDataType fb_type = (body.type != VM_TYPE_VOID) ? body.type : expected_type;
            if (c_match(c, TOK_OR) == VM_TRUE) {
                if ((c_check(c, TOK_IDENT) == VM_TRUE && lexer_peek(&c->lex).type == TOK_LBRACE) ||
                    c_check(c, TOK_LBRACE) == VM_TRUE) {
                    c_begin_scope(c);
                    if (c_check(c, TOK_IDENT) == VM_TRUE) {
                        Token err_tok = c->cur;
                        c_advance(c);
                        LocalVar *lv = &c->locals[c->num_locals++];
                        lv->name = err_tok.start;
                        lv->length = err_tok.length;
                        lv->type = VM_TYPE_STRING;
                        lv->reg = err_reg;
                        lv->depth = c->scope_depth;
                        lv->is_const = VM_TRUE;
                        lv->const_val = (VMReg){0};
                        lv->elem_count = 1;
                        lv->elem_types[0] = VM_TYPE_STRING;
                        lv->struct_id = -1;
                        lv->lambda_func_id = -1;
                        lv->lambda_gen_id = -1;
                        lv->func_type_id = -1;
                    }
                    c_consume(c, TOK_LBRACE, "Expected '{' in try..or catch block");
                    while (c_check(c, TOK_RBRACE) == VM_FALSE && c_check(c, TOK_EOF) == VM_FALSE) {
                        if (c_match(c, TOK_FAT_ARROW) == VM_TRUE) {
                            ExprResult fb = c_compile_expr(c, fb_type);
                            if (fb.reg != dst) {
                                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, fb.reg, 0));
                            }
                            if (body.type == VM_TYPE_VOID) body.type = fb.type;
                            c_match(c, TOK_SEMICOLON);
                        } else {
                            c_compile_statement(c);
                        }
                    }
                    c_consume(c, TOK_RBRACE, "Expected '}' after try..or catch block");
                    c_end_scope(c);
                } else {
                    ExprResult fb = compile_cmp(c, fb_type);
                    if (fb.reg != dst) {
                        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, fb.reg, 0));
                    }
                    if (body.type == VM_TYPE_VOID) body.type = fb.type;
                }
            }

            vm_i16 end_off = (vm_i16)(c->chunk->code_count - (end_jmp + 1));
            c->chunk->code[end_jmp] = VM_MAKE_ABx(OP_JMP, 0, end_off);
            body.reg = dst;
            body.is_const = VM_FALSE;
            body.elem_count = 1;
            body.elem_types[0] = body.type;
            return body;
        }
    }
    ExprResult left = compile_cmp(c, expected_type);
    while (c_check(c, TOK_LAND) == VM_TRUE || c_check(c, TOK_LOR) == VM_TRUE || c_check(c, TOK_OR) == VM_TRUE) {
        TokenType op_tok = c->cur.type;
        c_advance(c);
        if (op_tok == TOK_OR) {
            VMDataType val_type = (left.elem_count >= 2) ? left.elem_types[1] : left.type;
            vm_u8 payload_src = (left.elem_count >= 2) ? (vm_u8)(left.reg + 1) : left.reg;
            vm_u8 dst = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, payload_src, 0));
            vm_u32 jmp_ok = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP_TRUE, left.reg, 0));
            ExprResult fb = compile_cmp(c, val_type);
            if (fb.reg != dst) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, fb.reg, 0));
            }
            vm_i16 off = (vm_i16)(c->chunk->code_count - (jmp_ok + 1));
            c->chunk->code[jmp_ok] = VM_MAKE_ABx(OP_JMP_TRUE, left.reg, off);
            left.reg = dst;
            left.type = val_type;
            left.elem_count = 1;
            left.elem_types[0] = val_type;
            left.is_const = VM_FALSE;
            continue;
        }
        ExprResult right = compile_cmp(c, VM_TYPE_BOOL);
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC((op_tok == TOK_LAND) ? OP_AND_BOOL : OP_OR_BOOL, dst, left.reg, right.reg));
        left.reg = dst;
        left.type = VM_TYPE_BOOL;
        left.is_const = VM_FALSE;
    }
    return left;
}
