#include "gen.h"
#include "../../vm/stdlib/vm_intrinsics.h"

/* ---------------------------------------------------------------------------
 * Tokens, positions & errors
 * ------------------------------------------------------------------------- */

const Token *gen_tok(const Gen *g, vm_u32 tok) {
    return &g->t->toks[tok];
}

Token gen_token(const Gen *g, vm_u32 tok) {
    return g->t->toks[tok];
}

/* Best token for line info: the node's own token, or the first child that has one */
vm_u32 gen_node_tok(const Gen *g, AstRef n) {
    for (int guard = 0; n != 0 && guard < 8; guard++) {
        const AstNode *nd = &g->t->nodes[n];
        if (nd->tok != 0) return nd->tok;
        n = nd->a;
    }
    return 0;
}

void gen_at(Gen *g, AstRef n) {
    vm_u32 tok = gen_node_tok(g, n);
    if (tok == 0) return;
    g->c->cur = g->t->toks[tok];
    if (g->c->cur.line > 0) g->c->chunk->current_line = (vm_u16)g->c->cur.line;
}

void gen_error(Gen *g, AstRef n, const char *msg) {
    gen_at(g, n);
    c_error(g->c, msg);
}

vm_bool gen_tok_is(const Gen *g, vm_u32 tok, const char *text) {
    const Token *k = gen_tok(g, tok);
    return vm_str_eq_cstr(k->start, k->length, text);
}

ExprResult gen_result(VMDataType type) {
    ExprResult r = {0, type, VM_FALSE, {0}, 1, {type}, -1, -1, -1, VM_FALSE, VM_FALSE};
    return r;
}

vm_u8 gen_struct_width(Gen *g, ExprResult r) {
    if (r.struct_id >= 0) return g->c->structs[r.struct_id].num_fields;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Jumps
 * ------------------------------------------------------------------------- */

vm_u32 gen_emit_jump(Gen *g, VMOpCode op, vm_u8 a) {
    return vm_chunk_emit(g->c->chunk, VM_MAKE_ABx(op, a, 0));
}

void gen_patch_jump_to(Gen *g, vm_u32 at, vm_u32 target) {
    VMInst old = g->c->chunk->code[at];
    vm_i16 off = (vm_i16)((vm_i32)target - (vm_i32)(at + 1));
    g->c->chunk->code[at] = VM_MAKE_ABx(VM_GET_OP(old), VM_GET_A(old), off);
}

void gen_patch_jump(Gen *g, vm_u32 at) {
    gen_patch_jump_to(g, at, g->c->chunk->code_count);
}

void gen_emit_jump_back(Gen *g, VMOpCode op, vm_u8 a, vm_u32 target) {
    vm_i16 off = (vm_i16)((vm_i32)target - (vm_i32)(g->c->chunk->code_count + 1));
    vm_chunk_emit(g->c->chunk, VM_MAKE_ABx(op, a, off));
}

/* Copies all registers of `r` (struct fields / tuple slots) to `dst`; returns dst */
vm_u8 gen_move_into(Gen *g, ExprResult r, vm_u8 dst) {
    vm_u8 n = (r.struct_id >= 0) ? g->c->structs[r.struct_id].num_fields : r.elem_count;
    if (n == 0) n = 1;
    for (vm_u8 k = 0; k < n; k++) {
        if ((vm_u8)(r.reg + k) != (vm_u8)(dst + k)) {
            vm_chunk_emit(g->c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(dst + k), (vm_u8)(r.reg + k), 0));
        }
    }
    return dst;
}

/* ---------------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------------- */

static VMDataType struct_first_type(Compiler *c, int sid) {
    return (c->structs[sid].num_fields > 0) ? c->structs[sid].field_types[0] : VM_TYPE_I32;
}

/* Registers an inline function type `(x: A): R` as an anonymous func-type contract */
int gen_func_type(Gen *g, AstRef ty) {
    Compiler *c = g->c;
    const AstNode *nd = &g->t->nodes[ty];
    int ftid = (c->num_type_contracts < 32) ? c->num_type_contracts++ : 31;
    TypeContract *tc = &c->type_contracts[ftid];
    tc->name[0] = '$'; tc->name[1] = 'f'; tc->name[2] = 't'; tc->name[3] = '\0';
    tc->is_alias = VM_TRUE;
    tc->is_func_type = VM_TRUE;
    tc->alias_type = VM_TYPE_I32;
    tc->alias_struct_id = -1;
    tc->layout_struct_id = -1;
    tc->last_impl_struct_id = -1;
    tc->forward_func_id = -1;
    tc->func_arity = 0;
    tc->num_req_fields = 0;
    tc->num_req_methods = 0;
    AstList params = nd->a;
    AstRef ret = nd->b;
    for (vm_u32 i = 0; i < ast_list_len(g->t, params); i++) {
        AstRef p = ast_list_at(g->t, params, i);
        AstRef pty = (g->t->nodes[p].kind == AST_PARAM) ? g->t->nodes[p].a : p;
        int psid = -1;
        VMDataType pt = gen_type(g, pty, &psid, VM_NULL, VM_NULL);
        if (tc->func_arity < 8) {
            tc->func_param_types[tc->func_arity] = pt;
            tc->func_param_sids[tc->func_arity] = psid;
            tc->func_arity++;
        }
    }
    int rsid = -1;
    tc->func_ret_type = (ret != 0) ? gen_type(g, ret, &rsid, VM_NULL, VM_NULL) : VM_TYPE_VOID;
    tc->func_ret_sid = rsid;
    return ftid;
}

static VMDataType named_type(Gen *g, vm_u32 tok, int *out_sid, int *out_ftid) {
    Compiler *c = g->c;
    const Token *k = gen_tok(g, tok);
    switch (k->type) {
        case TOK_TYPE_I32:    return vm_str_eq_cstr(k->start, k->length, "int") ? VM_TYPE_I64 : VM_TYPE_I32;
        case TOK_TYPE_I64:    return VM_TYPE_I64;
        case TOK_TYPE_F32:    return VM_TYPE_F32;
        case TOK_TYPE_F64:    return VM_TYPE_F64;
        case TOK_TYPE_BOOL:   return VM_TYPE_BOOL;
        case TOK_TYPE_STRING: return VM_TYPE_STRING;
        case TOK_TYPE_VOID:   return VM_TYPE_VOID;
        default: break;
    }

    if (k->type == TOK_SELF_TYPE) {
        if (c->current_struct_id < 0) return VM_TYPE_I32;
        const char *sname = c->structs[c->current_struct_id].name;
        vm_u32 slen = vm_str_len(sname);
        for (int pt = 0; pt < VM_TYPE_COUNT; pt++) {
            const char *pname = c->chunk->primitive_struct_name[pt];
            if (pname[0] != '\0' && vm_str_eq(sname, slen, pname, vm_str_len(pname)) == VM_TRUE) return (VMDataType)pt;
        }
        *out_sid = c->current_struct_id;
        return struct_first_type(c, c->current_struct_id);
    }

    if (vm_str_eq_cstr(k->start, k->length, "array") == VM_TRUE) return VM_TYPE_ARRAY;
    if (vm_str_eq_cstr(k->start, k->length, "map") == VM_TRUE)   return VM_TYPE_MAP;
    for (int pt = 0; pt < VM_TYPE_COUNT; pt++) {
        const char *pname = c->chunk->primitive_struct_name[pt];
        if (pname[0] != '\0' && vm_str_eq(k->start, k->length, pname, vm_str_len(pname)) == VM_TRUE) return (VMDataType)pt;
    }

    int tid = c_resolve_type_contract(c, k->start, k->length);
    if (tid >= 0) {
        TypeContract *tc = &c->type_contracts[tid];
        if (tc->is_func_type == VM_TRUE) {
            *out_ftid = tid;
            return VM_TYPE_I32;
        }
        if (tc->is_alias == VM_TRUE) {
            *out_sid = tc->alias_struct_id;
            return tc->alias_type;
        }
        if (c->current_struct_id >= 0 &&
            c->structs[c->current_struct_id].owner_type_id != tid &&
            c_does_struct_implement_type(c, c->current_struct_id, tid) == VM_TRUE) {
            *out_sid = c->current_struct_id;
            return struct_first_type(c, c->current_struct_id);
        }
        if (tc->last_impl_struct_id >= 0) {
            *out_sid = tc->last_impl_struct_id;
            return struct_first_type(c, tc->last_impl_struct_id);
        }
        if (tc->layout_struct_id >= 0) {
            *out_sid = tc->layout_struct_id;
            return struct_first_type(c, tc->layout_struct_id);
        }
        return VM_TYPE_I32;
    }

    int sid = c_resolve_struct(c, k->start, k->length);
    if (sid >= 0) {
        *out_sid = sid;
        return struct_first_type(c, sid);
    }
    /* Opaque handle types registered by a host module (vml_module_type) */
    for (vm_u32 i = 0; i < c->chunk->num_host_types; i++) {
        if (vm_str_eq_cstr(k->start, k->length, c->chunk->host_types[i].name) == VM_TRUE) return c->chunk->host_types[i].type;
    }
    for (vm_u8 i = 0; i < c->num_active_tparams; i++) {
        vm_u32 plen = vm_str_len(c->active_tparam_names[i]);
        if (vm_str_eq(c->active_tparam_names[i], plen, k->start, k->length) == VM_TRUE) {
            *out_sid = c->active_tparam_sids[i];
            return c->active_tparam_types[i];
        }
    }
    return VM_TYPE_I32;
}

/*
 * Resolves a type node. out_sid: struct id (or -1); out_elem: element type of `T[]`
 * (VOID when unknown); out_ftid: function-type contract id (or -1).
 */
VMDataType gen_type(Gen *g, AstRef ty, int *out_sid, VMDataType *out_elem, int *out_ftid) {
    int sid_dummy = -1, ftid_dummy = -1;
    VMDataType elem_dummy = VM_TYPE_VOID;
    if (out_sid == VM_NULL) out_sid = &sid_dummy;
    if (out_elem == VM_NULL) out_elem = &elem_dummy;
    if (out_ftid == VM_NULL) out_ftid = &ftid_dummy;
    *out_sid = -1;
    *out_elem = VM_TYPE_VOID;
    *out_ftid = -1;
    if (ty == 0) return VM_TYPE_I32;

    const AstNode *nd = &g->t->nodes[ty];
    switch ((AstKind)nd->kind) {
        case AST_TY_NAME:
            return named_type(g, nd->tok, out_sid, out_ftid);
        case AST_TY_ARRAY: {
            int isid = -1;
            VMDataType inner = gen_type(g, nd->a, &isid, VM_NULL, VM_NULL);
            *out_elem = (isid >= 0) ? VM_TYPE_VOID : inner;
            return VM_TYPE_ARRAY;
        }
        case AST_TY_MAP: {
            VMDataType v = gen_type(g, nd->b, VM_NULL, VM_NULL, VM_NULL);
            *out_elem = v;
            return VM_TYPE_MAP;
        }
        case AST_TY_TUPLE: {
            if (ast_list_len(g->t, nd->a) == 0) return VM_TYPE_VOID;
            return gen_type(g, ast_list_at(g->t, nd->a, 0), VM_NULL, VM_NULL, VM_NULL);
        }
        case AST_TY_FUNC:
            *out_ftid = gen_func_type(g, ty);
            return VM_TYPE_I32;
        case AST_TY_RESULT: {
            int vsid = -1, esid = -1;
            VMDataType velem = VM_TYPE_VOID;
            VMDataType vt = (nd->a != 0) ? gen_type(g, nd->a, &vsid, &velem, VM_NULL) : VM_TYPE_VOID;
            VMDataType et = gen_type(g, nd->b, &esid, VM_NULL, VM_NULL);
            *out_sid = gen_result_sid(g, vt, vsid, et, esid, ty);
            if (*out_sid >= 0 && vt == VM_TYPE_ARRAY) g->c->structs[*out_sid].res_val_elem = velem;
            return VM_TYPE_BOOL;
        }
        case AST_TY_OPTIONAL: {
            int isid = -1;
            VMDataType inner = gen_type(g, nd->a, &isid, VM_NULL, VM_NULL);
            *out_sid = gen_optional_sid(g, inner, isid, ty);
            return VM_TYPE_BOOL;
        }
        default:
            return VM_TYPE_I32;
    }
}

/* ---------------------------------------------------------------------------
 * Calls
 * ------------------------------------------------------------------------- */

/* Fills type info of a call result whose return registers start at `base` */
void gen_set_res_from_fn(Gen *g, ExprResult *res, int fid, vm_u8 base) {
    Compiler *c = g->c;
    VMFunctionInfo *fn = &c->chunk->functions[fid];
    vm_u8 ret_slots = (fn->return_reg_count > 0) ? fn->return_reg_count : 1;
    res->reg = base;
    res->type = fn->return_type;
    res->struct_id = fn->return_struct_id;
    res->elem_count = ret_slots;
    res->is_const = VM_FALSE;
    res->lambda_func_id = -1;
    res->lambda_gen_id = -1;
    if (res->struct_id >= 0) {
        StructDef *sd = &c->structs[res->struct_id];
        for (vm_u8 f = 0; f < sd->num_fields; f++) res->elem_types[f] = sd->field_types[f];
    } else if (ret_slots > 1) {
        for (vm_u8 r = 0; r < ret_slots; r++) res->elem_types[r] = fn->return_elem_types[r];
    } else {
        res->elem_types[0] = res->type;
        if (res->type == VM_TYPE_ARRAY && g->ret_elem[fid] != VM_TYPE_VOID) res->elem_types[0] = g->ret_elem[fid];
    }
    if (fn->return_func_type_id >= 0) EXPR_SET_FUNC_TYPE(*res, fn->return_func_type_id);
}

/* Converts any value to a string register (struct `operator string`, or OP_CAST) */
vm_u8 gen_to_string_reg(Gen *g, ExprResult val) {
    Compiler *c = g->c;
    if (gen_is_optional(g, val)) return gen_optional_to_string(g, val);
    if (gen_is_result(g, val)) {
        gen_error(g, 0, "A 'T ! E' result must be handled with '?' or 'catch' before use");
        return val.reg;
    }
    if (gen_is_enum(g, val) && c_resolve_struct_method(c, val.struct_id, "op_str", 6) < 0) return gen_enum_to_string(g, val);
    if (val.struct_id >= 0) {
        int str_fid = c_resolve_struct_method(c, val.struct_id, "op_str", 6);
        if (str_fid >= 0) {
            ExprResult r = gen_call_fid(g, str_fid, &val, 1);
            return r.reg;
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

/*
 * Emits a call to `fid` with already-evaluated arguments. Struct arguments are
 * passed flattened (one register per field); missing trailing args use defaults.
 */
ExprResult gen_call_fid(Gen *g, int fid, const ExprResult *args_in, vm_u8 argc) {
    return gen_call_value(g, fid, args_in, argc, -1);
}

/* Like gen_call_fid, but when fn_reg >= 0 the callee is the function value in that register
 * (closures) and `fid` only supplies the signature. */
ExprResult gen_call_value(Gen *g, int fid, const ExprResult *args_in, vm_u8 argc, int fn_reg) {
    Compiler *c = g->c;
    ExprResult args[16];
    for (vm_u8 i = 0; i < argc && i < 16; i++) args[i] = args_in[i];
    VMFunctionInfo *fn = &c->chunk->functions[fid];

    if (fn->coerce_args_to_string == VM_TRUE) {
        for (vm_u8 i = 1; i < argc; i++) {
            args[i].reg = gen_to_string_reg(g, args[i]);
            args[i].type = VM_TYPE_STRING;
            args[i].struct_id = -1;
            args[i].elem_count = 1;
        }
    }

    /* Plain values / none passed for `T?` parameters are wrapped (before the call window is reserved) */
    for (vm_u8 i = 0; i < argc && i < fn->arity; i++) {
        int psid = fn->param_struct_ids[i];
        if (psid >= 0 && c->structs[psid].is_optional && args[i].struct_id != psid) args[i] = gen_wrap_optional(g, args[i], psid);
    }
    /* Numeric arguments take the parameter's type (before the call window is reserved) */
    for (vm_u8 i = 0; i < argc && i < fn->arity; i++) {
        if (fn->param_struct_ids[i] < 0 && args[i].struct_id < 0 && fn->coerce_args_to_string == VM_FALSE) {
            args[i] = gen_convert(g, args[i], fn->param_types[i]);
        }
    }
    /* Opcode intrinsics are inlined: arguments in a fresh window, the ops run there */
    const VMIntrinsic *intr = (fn_reg < 0) ? g->inline_intr[fid] : VM_NULL;
    if (intr != VM_NULL) {
        vm_u8 base = c_alloc_reg(c);
        for (vm_u8 i = 1; i < fn->arity; i++) c_alloc_reg(c);
        for (vm_u8 i = 0; i < fn->arity; i++) {
            vm_u8 src = (i < argc) ? args[i].reg : c_emit_load_const(c, fn->param_defaults[i], fn->param_types[i]);
            if (src != (vm_u8)(base + i)) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(base + i), src, 0));
        }
        for (vm_u8 k = 0; k < intr->num_ops; k++) {
            VMInst op = intr->ops[k];
            vm_u8 cc = intr->c_is_imm ? (vm_u8)VM_GET_C(op) : (vm_u8)(base + VM_GET_C(op));
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(VM_GET_OP(op), (vm_u8)(base + VM_GET_A(op)), (vm_u8)(base + VM_GET_B(op)), cc));
        }
        c->next_reg = (vm_u8)(base + 1);
        if (c->next_reg > c->max_reg) c->max_reg = c->next_reg;
        ExprResult res = gen_result(VM_TYPE_I32);
        gen_set_res_from_fn(g, &res, fid, base);
        return res;
    }

    vm_u8 ret_slots = (fn->return_reg_count > 0) ? fn->return_reg_count : 1;
    vm_u8 call_base = c_alloc_reg(c);
    for (vm_u8 r = 1; r < ret_slots; r++) c_alloc_reg(c);

    vm_u8 cursor = (vm_u8)(call_base + ret_slots);
    for (vm_u8 i = 0; i < fn->arity; i++) {
        if (i < argc) {
            vm_u8 count = 1;
            vm_u8 field_off = 0;
            int psid = fn->param_struct_ids[i];
            if (psid >= 0) {
                count = c->structs[psid].num_fields;
                /* Child struct passed for a parent-typed parameter: pass the parent's field window */
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
                (void)c_alloc_reg(c);
                vm_u8 src_r = (vm_u8)(args[i].reg + field_off + k);
                if (src_r != cursor) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor, src_r, 0));
                cursor++;
            }
        } else {
            (void)c_alloc_reg(c);
            vm_u8 kreg = c_emit_load_const(c, fn->param_defaults[i], fn->param_types[i]);
            if (kreg != cursor) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, cursor, kreg, 0));
            cursor++;
        }
    }

    c->next_reg = (vm_u8)(call_base + ret_slots);
    if (fn_reg >= 0) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CALL_REG, call_base, (vm_u8)fn_reg, 0));
    else vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CALL, call_base, fid));

    ExprResult res = gen_result(VM_TYPE_I32);
    gen_set_res_from_fn(g, &res, fid, call_base);
    return res;
}

/* Interns a compile-time string, copying it into the chunk (AST buffers do not outlive compilation) */
vm_u32 gen_const_string(Gen *g, const char *s, vm_u32 len) {
    VMChunk *chunk = g->c->chunk;
    char *dst = vm_chunk_arena_reserve(chunk, len);
    if (dst == VM_NULL) return 0;
    for (vm_u32 i = 0; i < len; i++) dst[i] = s[i];
    dst[len] = '\0';
    vm_u32 before = chunk->num_strings;
    vm_u32 sid = vm_chunk_add_string(chunk, dst, len);
    if (sid >= before) chunk->string_arena_used += len + 1;   /* new string: keep the copy */
    return sid;
}

/* ---------------------------------------------------------------------------
 * Numeric conversion
 * ------------------------------------------------------------------------- */

vm_bool gen_is_numeric(VMDataType t) {
    return vm_bool_from_int(t == VM_TYPE_I32 || t == VM_TYPE_I64 || t == VM_TYPE_F32 || t == VM_TYPE_F64);
}

static int numeric_rank(VMDataType t) {
    switch (t) {
        case VM_TYPE_I32: return 1;
        case VM_TYPE_I64: return 2;
        case VM_TYPE_F32: return 3;
        case VM_TYPE_F64: return 4;
        default:          return 0;
    }
}

/* Common type of a binary operation on two numbers (the wider one) */
VMDataType gen_wider(VMDataType a, VMDataType b) {
    return (numeric_rank(a) >= numeric_rank(b)) ? a : b;
}

/* Converts a numeric value to `to` (constants fold at compile time); anything else is returned unchanged */
ExprResult gen_convert(Gen *g, ExprResult r, VMDataType to) {
    Compiler *c = g->c;
    if (r.type == to || r.struct_id >= 0 || r.elem_count > 1 || !gen_is_numeric(to) || !gen_is_numeric(r.type)) return r;
    if (r.is_const) {
        vm_f64 fv = (r.type == VM_TYPE_F64) ? r.const_val.f64 : (r.type == VM_TYPE_F32) ? (vm_f64)r.const_val.f32 : 0.0;
        vm_i64 iv = (r.type == VM_TYPE_I64) ? r.const_val.i64 : (r.type == VM_TYPE_I32) ? (vm_i64)r.const_val.i32 : (vm_i64)fv;
        vm_bool from_float = vm_bool_from_int(r.type == VM_TYPE_F32 || r.type == VM_TYPE_F64);
        VMReg v = {0};
        switch (to) {
            case VM_TYPE_I32: v.i32 = (vm_i32)iv; break;
            case VM_TYPE_I64: v.i64 = iv; break;
            case VM_TYPE_F32: v.f32 = from_float ? (vm_f32)fv : (vm_f32)iv; break;
            default:          v.f64 = from_float ? fv : (vm_f64)iv; break;
        }
        r.const_val = v;
        r.type = to;
        r.elem_types[0] = to;
        /* If the constant was the last thing emitted, replace that load instead of leaving it dead */
        if (c->chunk->code_count > 0) {
            VMInst last = c->chunk->code[c->chunk->code_count - 1];
            VMOpCode lop = (VMOpCode)VM_GET_OP(last);
            if ((lop == OP_LOAD_IMM_I32 || lop == OP_LOAD_IMM_I64 || lop == OP_LOADK) && VM_GET_A(last) == r.reg &&
                c->next_reg == (vm_u8)(r.reg + 1)) {
                c->chunk->code_count--;
                c->next_reg = r.reg;
            }
        }
        r.reg = c_emit_load_const(c, v, to);
        return r;
    }
    vm_u8 dst = c_alloc_reg(c);
    if (r.type == VM_TYPE_I32 && to == VM_TYPE_I64) {
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_I32_TO_I64, dst, r.reg, 0));
    } else if (r.type == VM_TYPE_I64 && to == VM_TYPE_I32) {
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_I64_TO_I32, dst, r.reg, 0));
    } else {
        vm_u8 pair = (vm_u8)(((vm_u8)r.type << 4) | ((vm_u8)to & 0x0Fu));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CAST, dst, r.reg, pair));
    }
    r.reg = dst;
    r.type = to;
    r.elem_types[0] = to;
    r.is_const = VM_FALSE;
    return r;
}

/* "<prefix> '<name>'" into out (error messages) */
void gen_quoted_msg(char *out, vm_u32 cap, const char *prefix, const char *name, vm_u32 nlen) {
    vm_u32 k = 0;
    for (vm_u32 i = 0; prefix[i] && k < cap - 4; i++) out[k++] = prefix[i];
    out[k++] = ' ';
    out[k++] = '\'';
    for (vm_u32 i = 0; i < nlen && k < cap - 2; i++) out[k++] = name[i];
    out[k++] = '\'';
    out[k] = '\0';
}
