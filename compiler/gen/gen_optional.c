#include "gen.h"

/*
 * Optional values `T?`
 *
 * An optional is a synthetic two-field struct {has: bool, value: T}, so it moves through
 * locals, parameters and returns with the existing struct machinery (two registers).
 * A plain T (or `none`) is wrapped automatically where a T? is expected; going the
 * other way needs `??`, `if let`, or `?.`.
 */

#define ND(g, n)     (&(g)->t->nodes[(n)])
#define LLEN(g, l)   ast_list_len((g)->t, (l))
#define LAT(g, l, i) ast_list_at((g)->t, (l), (i))

vm_bool gen_is_optional(Gen *g, ExprResult r) {
    return vm_bool_from_int(r.struct_id >= 0 && g->c->structs[r.struct_id].is_optional);
}

vm_bool gen_sid_is_optional(Gen *g, int sid) {
    return vm_bool_from_int(sid >= 0 && g->c->structs[sid].is_optional);
}

/* Struct id of `inner?` (created on first use) */
int gen_optional_sid(Gen *g, VMDataType inner, int inner_sid, AstRef at) {
    Compiler *c = g->c;
    if (inner_sid >= 0) {
        gen_error(g, at, "Optional structs are not supported yet (only optional numbers, bool, string, arrays, maps)");
        return -1;
    }
    for (int i = 0; i < c->num_structs; i++) {
        if (c->structs[i].is_optional && c->structs[i].opt_inner == inner) return i;
    }
    if (c->num_structs >= 64) {
        gen_error(g, at, "Too many struct types");
        return -1;
    }
    int sid = c->num_structs++;
    StructDef *sd = &c->structs[sid];
    char name[40] = "$opt_";
    const char *tn = vm_type_name(inner);
    vm_u32 k = 5;
    for (vm_u32 i = 0; tn[i] && k < 39; i++) name[k++] = tn[i];
    name[k] = '\0';
    vm_str_copy(sd->name, name, k, 64);
    sd->owner_type_id = -1;
    sd->parent_struct_id = -1;
    sd->num_parents = 0;
    sd->num_implemented_types = 0;
    sd->num_fields = 2;
    vm_str_copy(sd->field_names[0], "has", 3, 32);
    vm_str_copy(sd->field_names[1], "value", 5, 32);
    sd->field_types[0] = VM_TYPE_BOOL;
    sd->field_types[1] = inner;
    sd->field_is_pub[0] = sd->field_is_pub[1] = VM_TRUE;
    sd->field_has_default[0] = sd->field_has_default[1] = VM_FALSE;
    sd->field_defaults[0] = sd->field_defaults[1] = (VMReg){0};
    sd->is_generic = VM_FALSE;
    sd->num_type_params = 0;
    sd->is_optional = VM_TRUE;
    sd->opt_inner = inner;
    sd->is_enum = VM_FALSE;
    sd->is_result = VM_FALSE;
    return sid;
}

static ExprResult optional_result(Gen *g, int sid, vm_u8 base) {
    ExprResult r = gen_result(VM_TYPE_BOOL);
    r.reg = base;
    r.struct_id = sid;
    r.elem_count = 2;
    r.elem_types[0] = VM_TYPE_BOOL;
    r.elem_types[1] = g->c->structs[sid].opt_inner;
    return r;
}

/* T -> T? (has = true), none -> T? (has = false) */
ExprResult gen_wrap_optional(Gen *g, ExprResult r, int sid) {
    Compiler *c = g->c;
    VMDataType inner = c->structs[sid].opt_inner;
    if (!r.is_none) r = gen_convert(g, r, inner);
    vm_u8 base = c_alloc_reg(c);
    vm_u8 slot = c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LOAD_BOOL, base, r.is_none ? 0 : 1, 0));
    if (r.is_none) {
        VMReg zero = {0};
        if (inner == VM_TYPE_STRING) zero.str_id = vm_chunk_add_string(c->chunk, "", 0);
        vm_u8 z = c_emit_load_const(c, zero, inner);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, slot, z, 0));
    } else {
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, slot, r.reg, 0));
    }
    c->next_reg = (vm_u8)(slot + 1);
    return optional_result(g, sid, base);
}

/*
 * Makes `r` fit a destination of (type, sid): wraps into an optional, converts numbers,
 * and rejects using an optional (or `none`) where a plain value is required.
 */
ExprResult gen_coerce(Gen *g, ExprResult r, VMDataType type, int sid, AstRef at) {
    if (gen_sid_is_result(g, sid)) return gen_wrap_ok(g, r, sid, at);
    if (gen_is_result(g, r) && r.struct_id != sid) {
        gen_error(g, at, "A 'T ! E' result must be handled with '?' or 'catch' before use");
        return r;
    }
    if (gen_sid_is_optional(g, sid)) {
        if (r.struct_id == sid) return r;
        if (r.is_none || r.struct_id < 0) return gen_wrap_optional(g, r, sid);
        gen_error(g, at, "Value does not match the optional type");
        return r;
    }
    if (r.is_none) {
        gen_error(g, at, "'none' can only be used where an optional type (T?) is expected");
        return r;
    }
    if (gen_is_optional(g, r) && sid < 0) {
        gen_error(g, at, "Optional value must be unwrapped first (use '?\?', 'if let' or '?.')");
        return r;
    }
    if (sid < 0) {
        /* Numbers convert; other primitive mismatches (int -> string, bool -> int, ...) are errors */
        if (type != GEN_ANY && type != VM_TYPE_VOID && r.type != VM_TYPE_VOID && r.struct_id < 0 && r.type != type &&
            r.lambda_func_id < 0 && r.lambda_gen_id < 0 && EXPR_GET_FUNC_TYPE(r) < 0 && r.elem_count <= 1 &&
            !(gen_is_numeric(r.type) && gen_is_numeric(type))) {
            char msg[96];
            const char *tn = vm_type_name(type);
            gen_quoted_msg(msg, sizeof(msg), "Type mismatch: expected", tn, vm_str_len(tn));
            gen_error(g, at, msg);
            return r;
        }
        return gen_convert(g, r, type);
    }
    if (r.struct_id < 0 && r.type != VM_TYPE_VOID) {
        char msg[96];
        const char *sn = g->c->structs[sid].name;
        gen_quoted_msg(msg, sizeof(msg), "Type mismatch: expected", sn, vm_str_len(sn));
        gen_error(g, at, msg);
    }
    return r;
}

ExprResult gen_none(Gen *g) {
    ExprResult r = gen_result(VM_TYPE_VOID);
    r.is_none = VM_TRUE;
    r.reg = c_alloc_reg(g->c);
    vm_chunk_emit(g->c->chunk, VM_MAKE_ABC(OP_LOAD_BOOL, r.reg, 0, 0));
    return r;
}

/* `a ?? b` */
ExprResult gen_coalesce(Gen *g, AstRef n, VMDataType expected) {
    Compiler *c = g->c;
    ExprResult left = gen_expr(g, ND(g, n)->a, expected);
    if (left.is_none) return gen_expr(g, ND(g, n)->b, expected);
    if (!gen_is_optional(g, left)) {
        gen_error(g, n, "Left side of '?\?' must be an optional value (T?)");
        return left;
    }
    VMDataType inner = c->structs[left.struct_id].opt_inner;
    vm_u8 dst = c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, (vm_u8)(left.reg + 1), 0));
    vm_u32 jmp_has = gen_emit_jump(g, OP_JMP_TRUE, left.reg);
    ExprResult fb = gen_expr(g, ND(g, n)->b, inner);
    if (gen_is_optional(g, fb)) {
        gen_error(g, n, "Right side of '?\?' must be a plain value");
        return left;
    }
    fb = gen_convert(g, fb, inner);
    if (fb.reg != dst) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, fb.reg, 0));
    gen_patch_jump(g, jmp_has);
    ExprResult res = gen_result(inner);
    res.reg = dst;
    return res;
}

/* `obj?.member` / `obj?.method(args)`: none when obj is none, otherwise the member, as an optional */
ExprResult gen_opt_chain(Gen *g, AstRef n) {
    Compiler *c = g->c;
    ExprResult obj = gen_expr(g, ND(g, n)->a, GEN_ANY);
    if (!gen_is_optional(g, obj)) {
        gen_error(g, n, "'?.' needs an optional value on the left");
        return obj;
    }
    VMDataType inner = c->structs[obj.struct_id].opt_inner;
    vm_u8 base = c_alloc_reg(c);
    vm_u8 slot = c_alloc_reg(c);
    vm_u32 jmp_none = gen_emit_jump(g, OP_JMP_FALSE, obj.reg);

    ExprResult value = gen_result(inner);
    value.reg = (vm_u8)(obj.reg + 1);
    const Token *m = gen_tok(g, ND(g, n)->tok);
    ExprResult r;
    if (ND(g, n)->flags & AST_F_CALL_ARGS) {
        r = gen_method_on(g, n, value, m->start, m->length, ND(g, n)->b);
    } else if (vm_str_eq_cstr(m->start, m->length, "len") &&
               (inner == VM_TYPE_STRING || inner == VM_TYPE_ARRAY || inner == VM_TYPE_MAP)) {
        vm_u8 dst = c_alloc_reg(c);
        VMOpCode op = (inner == VM_TYPE_ARRAY) ? OP_ARR_LEN : (inner == VM_TYPE_MAP) ? OP_MAP_LEN : OP_STRLEN;
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(op, dst, value.reg, 0));
        r = gen_result(VM_TYPE_I32);
        r.reg = dst;
    } else {
        gen_error(g, n, "Unknown member after '?.' (use a method call like x?.upper() or x?.len)");
        return obj;
    }
    if (r.struct_id >= 0 && !gen_is_optional(g, r)) {
        gen_error(g, n, "'?.' results that are structs are not supported yet");
        return obj;
    }

    VMDataType rtype = gen_is_optional(g, r) ? c->structs[r.struct_id].opt_inner : r.type;
    int rsid = gen_optional_sid(g, rtype, -1, n);
    if (rsid < 0) return obj;
    if (gen_is_optional(g, r)) {
        /* chaining into another optional: flatten */
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, base, r.reg, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, slot, (vm_u8)(r.reg + 1), 0));
    } else {
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LOAD_BOOL, base, 1, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, slot, r.reg, 0));
    }
    vm_u32 jmp_end = gen_emit_jump(g, OP_JMP, 0);
    gen_patch_jump(g, jmp_none);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LOAD_BOOL, base, 0, 0));
    gen_patch_jump(g, jmp_end);
    c->next_reg = (vm_u8)(slot + 1);
    return optional_result(g, rsid, base);
}

/* String form of an optional: its value, or "none" */
vm_u8 gen_optional_to_string(Gen *g, ExprResult r) {
    Compiler *c = g->c;
    vm_u8 dst = c_alloc_reg(c);
    VMReg none_s = {0};
    none_s.str_id = gen_const_string(g, "none", 4);
    vm_u32 jmp_has = gen_emit_jump(g, OP_JMP_TRUE, r.reg);
    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOADK, dst, (vm_u16)vm_chunk_add_constant(c->chunk, none_s, VM_TYPE_STRING)));
    vm_u32 jmp_end = gen_emit_jump(g, OP_JMP, 0);
    gen_patch_jump(g, jmp_has);
    ExprResult v = gen_result(c->structs[r.struct_id].opt_inner);
    v.reg = (vm_u8)(r.reg + 1);
    vm_u8 s = gen_to_string_reg(g, v);
    if (s != dst) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, s, 0));
    gen_patch_jump(g, jmp_end);
    return dst;
}
