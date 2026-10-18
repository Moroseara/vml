#include "gen.h"

/*
 * Errors as values: `T ! E`
 *
 * A result is a synthetic flattened struct: {ok: bool, value slots of T, error slots of E}.
 * Like optionals it moves through locals, parameters and returns with the struct machinery,
 * so there is no runtime support and no cost when nothing fails.
 *
 *   throw e            return an error from a function whose return type is `T ! E`
 *   f()?               unwrap the value, or return the error to the caller
 *   f() catch v        unwrap the value, or use v
 *   f() catch e { .. } unwrap the value, or run the block (its last expression is the value)
 *
 * `catch` on an expression that is not a result traps runtime errors (panics) instead,
 * with `e` bound to the message string. This replaces pcall / xpcall.
 */

#define ND(g, n)     (&(g)->t->nodes[(n)])
#define LLEN(g, l)   ast_list_len((g)->t, (l))
#define LAT(g, l, i) ast_list_at((g)->t, (l), (i))

vm_bool gen_sid_is_result(Gen *g, int sid) {
    return vm_bool_from_int(sid >= 0 && g->c->structs[sid].is_result);
}

vm_bool gen_is_result(Gen *g, ExprResult r) {
    return gen_sid_is_result(g, r.struct_id);
}

static vm_u8 part_width(Compiler *c, VMDataType type, int sid) {
    if (sid >= 0) return c->structs[sid].num_fields;
    return (type == VM_TYPE_VOID) ? 0 : 1;
}

static void type_label(Compiler *c, VMDataType type, int sid, char *out, vm_u32 *k, vm_u32 cap) {
    const char *tn = (sid >= 0) ? c->structs[sid].name : vm_type_name(type);
    for (vm_u32 i = 0; tn[i] && *k + 1 < cap; i++) out[(*k)++] = tn[i];
}

/* Struct id of `T ! E` (created on first use) */
int gen_result_sid(Gen *g, VMDataType vt, int vsid, VMDataType et, int esid, AstRef at) {
    Compiler *c = g->c;
    if (gen_sid_is_result(g, vsid) || gen_sid_is_result(g, esid)) {
        gen_error(g, at, "A result type cannot contain another result type");
        return -1;
    }
    if (et == VM_TYPE_VOID && esid < 0) {
        gen_error(g, at, "The error type of 'T ! E' cannot be void");
        return -1;
    }
    for (int i = 0; i < c->num_structs; i++) {
        StructDef *s = &c->structs[i];
        if (s->is_result && s->res_val_type == vt && s->res_val_sid == vsid && s->res_err_type == et && s->res_err_sid == esid) return i;
    }
    vm_u8 vw = part_width(c, vt, vsid);
    vm_u8 ew = part_width(c, et, esid);
    if (1 + vw + ew > MAX_TUPLE_ELEMS) {
        gen_error(g, at, "Result type is too large (value and error together may use at most 7 fields)");
        return -1;
    }
    if (c->num_structs >= 64) {
        gen_error(g, at, "Too many struct types");
        return -1;
    }
    int sid = c->num_structs++;
    StructDef *sd = &c->structs[sid];
    char name[64];
    vm_u32 k = 0;
    name[k++] = '$';
    type_label(c, vt, vsid, name, &k, 30);
    name[k++] = '!';
    type_label(c, et, esid, name, &k, 62);
    name[k] = '\0';
    vm_str_copy(sd->name, name, k, 64);
    sd->owner_type_id = -1;
    sd->parent_struct_id = -1;
    sd->num_parents = 0;
    sd->num_implemented_types = 0;
    sd->is_generic = VM_FALSE;
    sd->num_type_params = 0;
    sd->is_optional = VM_FALSE;
    sd->is_enum = VM_FALSE;
    sd->is_result = VM_TRUE;
    sd->res_val_type = vt;
    sd->res_val_sid = vsid;
    sd->res_val_w = vw;
    sd->res_val_elem = VM_TYPE_VOID;
    sd->res_err_type = et;
    sd->res_err_sid = esid;
    sd->res_err_w = ew;

    vm_u8 n = 0;
    vm_str_copy(sd->field_names[n], "ok", 2, 32);
    sd->field_types[n++] = VM_TYPE_BOOL;
    for (vm_u8 f = 0; f < vw; f++) {
        vm_str_copy(sd->field_names[n], "value", 5, 32);
        sd->field_types[n++] = (vsid >= 0) ? c->structs[vsid].field_types[f] : vt;
    }
    for (vm_u8 f = 0; f < ew; f++) {
        vm_str_copy(sd->field_names[n], "error", 5, 32);
        sd->field_types[n++] = (esid >= 0) ? c->structs[esid].field_types[f] : et;
    }
    sd->num_fields = n;
    for (vm_u8 f = 0; f < n; f++) {
        sd->field_is_pub[f] = VM_FALSE;
        sd->field_has_default[f] = VM_FALSE;
        sd->field_defaults[f] = (VMReg){0};
    }
    return sid;
}

/* A view of one part (value or error) of a result held at `base` */
static ExprResult part_view(Gen *g, vm_u8 base, VMDataType type, int sid) {
    ExprResult r = gen_result(type);
    r.reg = base;
    if (sid >= 0) {
        StructDef *sd = &g->c->structs[sid];
        r.type = sd->field_types[0];
        r.struct_id = sid;
        r.elem_count = sd->num_fields;
        for (vm_u8 f = 0; f < sd->num_fields; f++) r.elem_types[f] = sd->field_types[f];
    }
    return r;
}

static ExprResult value_part(Gen *g, ExprResult r) {
    StructDef *sd = &g->c->structs[r.struct_id];
    ExprResult v = part_view(g, (vm_u8)(r.reg + 1), sd->res_val_type, sd->res_val_sid);
    if (sd->res_val_type == VM_TYPE_ARRAY && sd->res_val_elem != VM_TYPE_VOID) v.elem_types[0] = sd->res_val_elem;
    return v;
}

static ExprResult error_part(Gen *g, ExprResult r) {
    StructDef *sd = &g->c->structs[r.struct_id];
    return part_view(g, (vm_u8)(r.reg + 1 + sd->res_val_w), sd->res_err_type, sd->res_err_sid);
}

static ExprResult result_view(Gen *g, int sid, vm_u8 base) {
    StructDef *sd = &g->c->structs[sid];
    ExprResult r = gen_result(VM_TYPE_BOOL);
    r.reg = base;
    r.struct_id = sid;
    r.elem_count = sd->num_fields;
    for (vm_u8 f = 0; f < sd->num_fields; f++) r.elem_types[f] = sd->field_types[f];
    return r;
}

/* Makes `r` fit the value (or error) part of result `sid` */
static ExprResult fit_part(Gen *g, ExprResult r, VMDataType type, int sid, AstRef at, const char *what) {
    if (sid >= 0) {
        if (r.struct_id != sid) {
            char msg[96];
            gen_quoted_msg(msg, sizeof(msg), what, g->c->structs[sid].name, vm_str_len(g->c->structs[sid].name));
            gen_error(g, at, msg);
        }
        return r;
    }
    if (r.struct_id >= 0 || (r.type != type && !(gen_is_numeric(r.type) && gen_is_numeric(type)))) {
        char msg[96];
        const char *tn = vm_type_name(type);
        gen_quoted_msg(msg, sizeof(msg), what, tn, vm_str_len(tn));
        gen_error(g, at, msg);
        return r;
    }
    return gen_coerce(g, r, type, -1, at);
}

/* Builds a result in fresh registers: ok = true with `v`, or ok = false with error `v` */
static ExprResult build_result(Gen *g, int sid, vm_bool ok, ExprResult v, AstRef at) {
    Compiler *c = g->c;
    StructDef *sd = &c->structs[sid];
    if (ok) {
        if (sd->res_val_w > 0) v = fit_part(g, v, sd->res_val_type, sd->res_val_sid, at, "Expected a value of type");
    } else {
        v = fit_part(g, v, sd->res_err_type, sd->res_err_sid, at, "Expected an error of type");
    }
    vm_u8 base = c_alloc_reg(c);
    for (vm_u8 f = 1; f < sd->num_fields; f++) c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LOAD_BOOL, base, ok ? 1 : 0, 0));
    vm_u8 off = ok ? 1 : (vm_u8)(1 + sd->res_val_w);
    vm_u8 w = ok ? sd->res_val_w : sd->res_err_w;
    for (vm_u8 f = 0; f < w; f++) {
        vm_u8 dst = (vm_u8)(base + off + f);
        if ((vm_u8)(v.reg + f) != dst) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, (vm_u8)(v.reg + f), 0));
    }
    c->next_reg = (vm_u8)(base + sd->num_fields);
    if (c->next_reg > c->max_reg) c->max_reg = c->next_reg;
    return result_view(g, sid, base);
}

/* A plain T becomes a successful `T ! E` */
ExprResult gen_wrap_ok(Gen *g, ExprResult r, int sid, AstRef at) {
    if (r.struct_id == sid) return r;
    if (gen_is_result(g, r)) {
        gen_error(g, at, "Result types do not match (the error types differ: use '?' or 'catch')");
        return r;
    }
    return build_result(g, sid, VM_TRUE, r, at);
}

/* `return` with no value in a `void ! E` function */
ExprResult gen_ok_void(Gen *g, int sid) {
    ExprResult none = gen_result(VM_TYPE_VOID);
    return build_result(g, sid, VM_TRUE, none, 0);
}

static void emit_return_result(Gen *g, ExprResult r) {
    Compiler *c = g->c;
    vm_u8 n = c->structs[r.struct_id].num_fields;
    if (c->next_reg < (vm_u8)(r.reg + n)) c->next_reg = (vm_u8)(r.reg + n);
    if (c->next_reg > c->max_reg) c->max_reg = c->next_reg;
    gen_emit_defers_for_return(g);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, r.reg, n, 0));
}

/* `throw e` */
void gen_throw(Gen *g, AstRef n) {
    Compiler *c = g->c;
    int rsid = c->current_ret_struct_id;
    if (!gen_sid_is_result(g, rsid)) {
        gen_error(g, n, "'throw' is only allowed in a function that returns 'T ! E' (use panic() for bugs)");
        return;
    }
    StructDef *sd = &c->structs[rsid];
    VMDataType want = (sd->res_err_sid >= 0) ? GEN_ANY : sd->res_err_type;
    ExprResult e = gen_expr(g, ND(g, n)->a, want);
    ExprResult r = build_result(g, rsid, VM_FALSE, e, n);
    emit_return_result(g, r);
}

/* `expr?` */
ExprResult gen_propagate(Gen *g, AstRef n) {
    Compiler *c = g->c;
    ExprResult r = gen_expr(g, ND(g, n)->a, GEN_ANY);
    if (!gen_is_result(g, r)) {
        gen_error(g, n, "'?' needs a value of type 'T ! E' on the left");
        return r;
    }
    int rsid = c->current_ret_struct_id;
    if (!gen_sid_is_result(g, rsid)) {
        gen_error(g, n, "'?' is only allowed in a function that returns 'T ! E' (use 'catch' here)");
        return r;
    }
    StructDef *src = &c->structs[r.struct_id];
    StructDef *dst = &c->structs[rsid];
    if (src->res_err_type != dst->res_err_type || src->res_err_sid != dst->res_err_sid) {
        gen_error(g, n, "'?' cannot pass this error on: its error type differs from the function's (use 'catch')");
        return r;
    }
    vm_u8 saved = c->next_reg;
    vm_u32 jmp_ok = gen_emit_jump(g, OP_JMP_TRUE, r.reg);
    if (r.struct_id == rsid) {
        emit_return_result(g, r);
    } else {
        emit_return_result(g, build_result(g, rsid, VM_FALSE, error_part(g, r), n));
    }
    c->next_reg = saved;
    gen_patch_jump(g, jmp_ok);
    return value_part(g, r);
}

/* Declares `name` as a local holding `v` (struct values use one register per field) */
static void bind_local(Gen *g, vm_u32 tok, ExprResult v) {
    Compiler *c = g->c;
    vm_u8 w = (v.struct_id >= 0) ? c->structs[v.struct_id].num_fields : 1;
    VMDataType t0 = (v.struct_id >= 0) ? c->structs[v.struct_id].field_types[0] : v.type;
    vm_u8 base = c_declare_local(c, gen_token(g, tok), t0, VM_FALSE, (VMReg){0});
    for (vm_u8 f = 1; f < w; f++) c_alloc_reg(c);
    LocalVar *lv = &c->locals[c->num_locals - 1];
    if (v.struct_id >= 0) {
        lv->struct_id = v.struct_id;
        lv->elem_count = w;
        for (vm_u8 f = 0; f < w; f++) lv->elem_types[f] = c->structs[v.struct_id].field_types[f];
    }
    for (vm_u8 f = 0; f < w; f++) {
        if ((vm_u8)(v.reg + f) != (vm_u8)(base + f)) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(base + f), (vm_u8)(v.reg + f), 0));
    }
}

/* A block whose last statement leaves (return / throw / break / continue) needs no value */
static vm_bool block_diverges(Gen *g, AstRef fb) {
    if (ND(g, fb)->kind != AST_BLOCK) return VM_FALSE;
    AstList stmts = ND(g, fb)->a;
    vm_u32 n = LLEN(g, stmts);
    if (n == 0) return VM_FALSE;
    vm_u8 k = ND(g, LAT(g, stmts, n - 1))->kind;
    return vm_bool_from_int(k == AST_RETURN || k == AST_THROW || k == AST_BREAK || k == AST_CONTINUE);
}

/* Value of the fallback: a block (last expression is the value) or an expression */
static ExprResult fallback_value(Gen *g, AstRef fb, VMDataType want, vm_bool *has_value) {
    *has_value = VM_TRUE;
    if (ND(g, fb)->kind == AST_BLOCK) return gen_block_value(g, fb, want, has_value);
    return gen_expr(g, fb, want);
}

static void move_width(Gen *g, ExprResult v, vm_u8 dst, vm_u8 w) {
    for (vm_u8 f = 0; f < w; f++) {
        if ((vm_u8)(v.reg + f) != (vm_u8)(dst + f)) vm_chunk_emit(g->c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(dst + f), (vm_u8)(v.reg + f), 0));
    }
}

/* `expr catch fallback` / `expr catch e { ... }` */
ExprResult gen_catch(Gen *g, AstRef n, VMDataType expected) {
    Compiler *c = g->c;
    AstRef fb = ND(g, n)->b;
    vm_u32 bind = ND(g, n)->tok;

    /* The trap is set up first; if the expression turns out to be a result it becomes a no-op jump */
    vm_u8 err_reg = c_alloc_reg(c);
    vm_u32 trap = gen_emit_jump(g, OP_TRY_BEGIN, err_reg);
    vm_u8 after_trap = c->next_reg;
    ExprResult r = gen_expr(g, ND(g, n)->a, expected);

    if (gen_is_result(g, r)) {
        c->chunk->code[trap] = VM_MAKE_ABx(OP_JMP, 0, 0);
        StructDef *sd = &c->structs[r.struct_id];
        vm_u8 w = sd->res_val_w;
        ExprResult val = value_part(g, r);
        vm_u8 dst = c_alloc_reg(c);
        for (vm_u8 f = 1; f < w; f++) c_alloc_reg(c);
        if (w > 0) move_width(g, val, dst, w);
        vm_u32 jmp_ok = gen_emit_jump(g, OP_JMP_TRUE, r.reg);
        vm_u8 keep = c->next_reg;
        gen_begin_scope(g);
        if (bind != 0) bind_local(g, bind, error_part(g, r));
        vm_bool has_v;
        VMDataType want = (sd->res_val_sid >= 0 || w == 0) ? GEN_ANY : sd->res_val_type;
        ExprResult f = fallback_value(g, fb, want, &has_v);
        if (w > 0 && !(!has_v && block_diverges(g, fb))) {
            if (!has_v) {
                gen_error(g, n, "The 'catch' block must end with a value");
            } else {
                f = fit_part(g, f, sd->res_val_type, sd->res_val_sid, n, "Expected a fallback of type");
                move_width(g, f, dst, w);
            }
        }
        gen_end_scope(g);
        c->next_reg = keep;
        gen_patch_jump(g, jmp_ok);
        if (w == 0) return gen_result(VM_TYPE_VOID);
        ExprResult out = part_view(g, dst, sd->res_val_type, sd->res_val_sid);
        out.elem_types[0] = val.elem_types[0];
        return out;
    }

    /* Not a result: trap runtime errors (panics); `e` is the message */
    if (r.struct_id >= 0) {
        gen_error(g, n, "'catch' around a struct value is only supported for 'T ! E' results");
        return r;
    }
    (void)after_trap;
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_TRY_END, 0, 0, 0));
    vm_u8 dst = c_alloc_reg(c);
    vm_bool has_value_type = vm_bool_from_int(r.type != VM_TYPE_VOID);
    if (has_value_type && r.reg != dst) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, r.reg, 0));
    vm_u32 end_jmp = gen_emit_jump(g, OP_JMP, 0);
    gen_patch_jump(g, trap);
    vm_u8 keep = c->next_reg;
    gen_begin_scope(g);
    if (bind != 0) {
        ExprResult msg = gen_result(VM_TYPE_STRING);
        msg.reg = err_reg;
        bind_local(g, bind, msg);
    }
    vm_bool has_v;
    ExprResult f = fallback_value(g, fb, has_value_type ? r.type : GEN_ANY, &has_v);
    if (has_value_type && !(!has_v && block_diverges(g, fb))) {
        if (!has_v) {
            gen_error(g, n, "The 'catch' block must end with a value");
        } else {
            f = gen_coerce(g, f, r.type, -1, n);
            if (f.reg != dst) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, f.reg, 0));
        }
    }
    gen_end_scope(g);
    c->next_reg = keep;
    gen_patch_jump(g, end_jmp);
    if (!has_value_type) return gen_result(VM_TYPE_VOID);
    ExprResult out = r;
    out.reg = dst;
    out.is_const = VM_FALSE;
    return out;
}
