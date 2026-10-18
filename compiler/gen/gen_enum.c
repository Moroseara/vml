#include "gen.h"

/*
 * Enums and `match`
 *
 * An enum value is a struct {tag: i32, p0, p1, ...}. `match` compares tags, binds
 * payload values, checks that every variant is handled (unless there is a `_` arm),
 * and can be used as an expression.
 */

#define ND(g, n)     (&(g)->t->nodes[(n)])
#define LLEN(g, l)   ast_list_len((g)->t, (l))
#define LAT(g, l, i) ast_list_at((g)->t, (l), (i))

vm_bool gen_is_enum(Gen *g, ExprResult r) {
    return vm_bool_from_int(r.struct_id >= 0 && g->c->structs[r.struct_id].is_enum);
}

static const char *variant_short_name(const char *qname) {
    const char *dot = qname;
    for (const char *p = qname; *p; p++) if (*p == '.') dot = p + 1;
    return dot;
}

/* Builds `Enum.Variant(args...)`; args may be 0 for variants without payload */
ExprResult gen_enum_value(Gen *g, AstRef at, int ev, AstList args) {
    Compiler *c = g->c;
    EnumVariant *v = &c->enum_variants[ev];
    int sid = v->enum_sid;
    StructDef *sd = &c->structs[sid];
    vm_u32 nargs = LLEN(g, args);
    if (nargs != v->payload_count) {
        char msg[128];
        vm_u32 k = 0;
        const char *pre = "Wrong number of values for enum variant ";
        for (vm_u32 i = 0; pre[i] && k < 100; i++) msg[k++] = pre[i];
        for (vm_u32 i = 0; v->qname[i] && k < 126; i++) msg[k++] = v->qname[i];
        msg[k] = '\0';
        gen_error(g, at, msg);
        return gen_result(VM_TYPE_I32);
    }
    /* Evaluate payload values first, then lay the struct out in fresh registers */
    ExprResult vals[MAX_VARIANT_PAYLOAD];
    for (vm_u32 i = 0; i < nargs; i++) {
        vals[i] = gen_coerce(g, gen_expr(g, LAT(g, args, i), v->payload_types[i]), v->payload_types[i], -1, at);
    }
    vm_u8 base = c_alloc_reg(c);
    for (vm_u8 f = 1; f < sd->num_fields; f++) c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, base, (vm_i16)v->tag));
    for (vm_u8 f = 1; f < sd->num_fields; f++) {
        vm_u8 dst = (vm_u8)(base + f);
        if (f - 1 < (int)nargs) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, vals[f - 1].reg, 0));
        else vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I64, dst, 0));
    }
    ExprResult r = gen_result(VM_TYPE_I32);
    r.reg = base;
    r.struct_id = sid;
    r.elem_count = sd->num_fields;
    for (vm_u8 f = 0; f < sd->num_fields; f++) r.elem_types[f] = sd->field_types[f];
    return r;
}

/* Variant of enum `sid` named `name` (short name, or qualified through any alias) */
static int find_variant(Gen *g, int sid, const char *name, vm_u32 len, vm_bool qualified) {
    Compiler *c = g->c;
    if (qualified) {
        int ev = c_resolve_enum_variant(c, name, len);
        return (ev >= 0 && c->enum_variants[ev].enum_sid == sid) ? ev : -1;
    }
    StructDef *sd = &c->structs[sid];
    for (int i = sd->enum_first; i < sd->enum_first + sd->enum_count; i++) {
        const char *short_name = variant_short_name(c->enum_variants[i].qname);
        if (vm_str_eq(short_name, vm_str_len(short_name), name, len) == VM_TRUE) return i;
    }
    return -1;
}

/* Pattern -> variant index, with the call arguments (bindings) if any. -1 if not a variant pattern. */
static int pattern_variant(Gen *g, int sid, AstRef pat, AstList *binds) {
    *binds = 0;
    AstRef head = pat;
    if (ND(g, pat)->kind == AST_CALL) {
        head = ND(g, pat)->a;
        *binds = ND(g, pat)->b;
    }
    if (ND(g, head)->kind == AST_IDENT) {
        const Token *k = gen_tok(g, ND(g, head)->tok);
        return find_variant(g, sid, k->start, k->length, VM_FALSE);
    }
    if (ND(g, head)->kind == AST_FIELD_GET && ND(g, ND(g, head)->a)->kind == AST_IDENT) {
        const Token *e = gen_tok(g, ND(g, ND(g, head)->a)->tok);
        const Token *v = gen_tok(g, ND(g, head)->tok);
        char qname[64];
        vm_str_join_dot(qname, 64, e->start, e->length, v->start, v->length);
        return find_variant(g, sid, qname, vm_str_len(qname), VM_TRUE);
    }
    return -1;
}

/* Value of a block used as a match arm: statements, then the final expression */
ExprResult gen_block_value(Gen *g, AstRef block, VMDataType expected, vm_bool *has_value) {
    AstList stmts = ND(g, block)->a;
    vm_u32 n = LLEN(g, stmts);
    ExprResult v = gen_result(VM_TYPE_VOID);
    *has_value = VM_FALSE;
    for (vm_u32 i = 0; i + 1 < n && !g->c->had_error; i++) gen_stmt(g, LAT(g, stmts, i));
    if (n > 0) {
        AstRef last = LAT(g, stmts, n - 1);
        if (ND(g, last)->kind == AST_EXPR_STMT) {
            v = gen_expr(g, ND(g, last)->a, expected);
            *has_value = VM_TRUE;
        } else {
            gen_stmt(g, last);
        }
    }
    return v;
}

ExprResult gen_match_any(Gen *g, AstRef n, vm_bool want_value, VMDataType expected) {
    Compiler *c = g->c;
    ExprResult target = gen_expr(g, ND(g, n)->a, GEN_ANY);
    vm_bool is_enum = gen_is_enum(g, target);
    if (target.struct_id >= 0 && !is_enum) {
        gen_error(g, n, gen_is_optional(g, target) ? "Use 'if let' or '?\?' to unwrap an optional, not 'match'"
                                                   : "'match' works on enums, numbers, strings and bools");
        return gen_result(VM_TYPE_I32);
    }
    VMOpCode eq_op = (target.type == VM_TYPE_I64) ? OP_EQ_I64 : (target.type == VM_TYPE_STRING) ? OP_EQ_STR :
                     (target.type == VM_TYPE_F32) ? OP_EQ_F32 : (target.type == VM_TYPE_F64) ? OP_EQ_F64 : OP_EQ_I32;

    vm_u8 dst = 0;
    VMDataType rtype = VM_TYPE_VOID;
    vm_bool have_rtype = VM_FALSE;
    if (want_value) dst = c_alloc_reg(c);

    AstList arms = ND(g, n)->b;
    vm_u32 end_jumps[64];
    int num_end = 0;
    vm_bool has_default = VM_FALSE;
    vm_bool covered[64] = {0};

    for (vm_u32 i = 0; i < LLEN(g, arms) && !c->had_error; i++) {
        AstRef arm = LAT(g, arms, i);
        AstRef pat = ND(g, arm)->a;
        AstRef body = ND(g, arm)->b;
        vm_u32 next_arm = 0xFFFFFFFFu;
        int ev = -1;
        AstList binds = 0;

        gen_begin_scope(g);
        if (pat == 0) {
            has_default = VM_TRUE;
        } else if (is_enum) {
            ev = pattern_variant(g, target.struct_id, pat, &binds);
            if (ev < 0) {
                char msg[128];
                gen_quoted_msg(msg, 128, "Pattern is not a variant of enum", c->structs[target.struct_id].name,
                               vm_str_len(c->structs[target.struct_id].name));
                gen_error(g, pat, msg);
                return gen_result(VM_TYPE_I32);
            }
            EnumVariant *v = &c->enum_variants[ev];
            if (LLEN(g, binds) != v->payload_count) {
                gen_error(g, pat, "Pattern binds a different number of values than the variant carries");
                return gen_result(VM_TYPE_I32);
            }
            int idx = ev - c->structs[target.struct_id].enum_first;
            if (idx >= 0 && idx < 64) covered[idx] = VM_TRUE;
            else {
                /* qualified through an export alias: mark by tag */
                StructDef *sd = &c->structs[target.struct_id];
                for (int k = 0; k < sd->enum_count && k < 64; k++) {
                    if (c->enum_variants[sd->enum_first + k].tag == v->tag) covered[k] = VM_TRUE;
                }
            }
            vm_u8 tag_reg = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, tag_reg, (vm_i16)v->tag));
            vm_u8 cmp_r = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_EQ_I32, cmp_r, target.reg, tag_reg));
            next_arm = gen_emit_jump(g, OP_JMP_FALSE, cmp_r);
            for (vm_u32 k = 0; k < LLEN(g, binds); k++) {
                AstRef b = LAT(g, binds, k);
                if (ND(g, b)->kind != AST_IDENT) {
                    gen_error(g, b, "Variant pattern arguments must be names (or _)");
                    return gen_result(VM_TYPE_I32);
                }
                const Token *bt = gen_tok(g, ND(g, b)->tok);
                if (vm_str_eq_cstr(bt->start, bt->length, "_")) continue;
                vm_u8 r = c_declare_local(c, *bt, v->payload_types[k], VM_FALSE, (VMReg){0});
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, r, (vm_u8)(target.reg + 1 + k), 0));
            }
        } else {
            ExprResult pv = gen_convert(g, gen_expr(g, pat, target.type), target.type);
            vm_u8 cmp_r = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(eq_op, cmp_r, target.reg, pv.reg));
            next_arm = gen_emit_jump(g, OP_JMP_FALSE, cmp_r);
        }

        if (want_value) {
            vm_bool has_v = VM_TRUE;
            ExprResult v = (ND(g, body)->kind == AST_BLOCK) ? gen_block_value(g, body, have_rtype ? rtype : expected, &has_v)
                                                            : gen_expr(g, body, have_rtype ? rtype : expected);
            if (!has_v || v.type == VM_TYPE_VOID) {
                gen_error(g, arm, "Every arm of a match expression must produce a value");
                return gen_result(VM_TYPE_I32);
            }
            if (v.struct_id >= 0) {
                gen_error(g, arm, "match expressions producing structs are not supported yet");
                return gen_result(VM_TYPE_I32);
            }
            if (!have_rtype) {
                rtype = v.type;
                have_rtype = VM_TRUE;
            }
            v = gen_convert(g, v, rtype);
            if (v.reg != dst) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, v.reg, 0));
        } else if (ND(g, body)->kind == AST_BLOCK) {
            gen_block(g, body);
        } else if (ND(g, body)->kind >= AST_BLOCK && ND(g, body)->kind <= AST_EXPR_STMT) {
            gen_stmt(g, body);
        } else {
            vm_u8 saved = c->next_reg;
            (void)gen_expr(g, body, VM_TYPE_VOID);
            c->next_reg = saved;
        }
        gen_end_scope(g);

        if (num_end < 64) end_jumps[num_end++] = gen_emit_jump(g, OP_JMP, 0);
        if (next_arm != 0xFFFFFFFFu) gen_patch_jump(g, next_arm);
        if (pat == 0) break;   /* `_` catches everything: later arms are unreachable */
    }

    if (is_enum && !has_default) {
        StructDef *sd = &c->structs[target.struct_id];
        for (int k = 0; k < sd->enum_count && k < 64; k++) {
            if (!covered[k]) {
                char msg[128];
                gen_quoted_msg(msg, 128, "match is not exhaustive: add an arm for",
                               c->enum_variants[sd->enum_first + k].qname, vm_str_len(c->enum_variants[sd->enum_first + k].qname));
                gen_error(g, n, msg);
                return gen_result(VM_TYPE_I32);
            }
        }
    }
    if (want_value && !is_enum && !has_default) {
        gen_error(g, n, "A match expression needs a '_' arm (the value must always be defined)");
        return gen_result(VM_TYPE_I32);
    }
    for (int i = 0; i < num_end; i++) gen_patch_jump(g, end_jumps[i]);

    ExprResult res = gen_result(want_value ? rtype : VM_TYPE_VOID);
    res.reg = dst;
    return res;
}

/* "Shape.Circle" for an enum value (tag lookup chain) */
vm_u8 gen_enum_to_string(Gen *g, ExprResult r) {
    Compiler *c = g->c;
    StructDef *sd = &c->structs[r.struct_id];
    vm_u8 dst = c_alloc_reg(c);
    vm_u32 ends[64];
    int ne = 0;
    for (int k = 0; k < sd->enum_count && k < 64; k++) {
        EnumVariant *v = &c->enum_variants[sd->enum_first + k];
        vm_u8 t = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, t, (vm_i16)v->tag));
        vm_u8 cmp = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_EQ_I32, cmp, r.reg, t));
        vm_u32 skip = gen_emit_jump(g, OP_JMP_FALSE, cmp);
        VMReg s = {0};
        s.str_id = gen_const_string(g, v->qname, vm_str_len(v->qname));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOADK, dst, (vm_u16)vm_chunk_add_constant(c->chunk, s, VM_TYPE_STRING)));
        ends[ne++] = gen_emit_jump(g, OP_JMP, 0);
        gen_patch_jump(g, skip);
    }
    VMReg unk = {0};
    unk.str_id = gen_const_string(g, sd->name, vm_str_len(sd->name));
    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOADK, dst, (vm_u16)vm_chunk_add_constant(c->chunk, unk, VM_TYPE_STRING)));
    for (int i = 0; i < ne; i++) gen_patch_jump(g, ends[i]);
    return dst;
}
