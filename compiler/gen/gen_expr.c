#include "gen.h"

#define T(g)        ((g)->t)
#define ND(g, n)    (&(g)->t->nodes[(n)])
#define LLEN(g, l)  ast_list_len((g)->t, (l))
#define LAT(g, l, i) ast_list_at((g)->t, (l), (i))

/* ---------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------- */

/* Name text of a token; `Self` maps to the current struct's name */
/* A module's own functions can only be named where that module is imported (directly, or shared
   by an imported module with `pub import`). Type methods (`Bytes.new`, `doc:get`) are not affected. */
static vm_bool module_not_imported(Gen *g, AstRef at, int fid) {
    Compiler *c = g->c;
    int owner = c->func_module[fid];
    if (owner < 0) return VM_FALSE;
    int caller = (c->current_func_id >= 0) ? c->func_module[c->current_func_id] : c->cur_module_idx;
    if (caller == owner || (c->prelude_mask & (1u << owner))) return VM_FALSE;
    if (c->mod_visible[caller + 1] & (1u << owner)) return VM_FALSE;
    const char *handle = c->loaded_modules[owner];
    vm_u32 hl = vm_str_len(handle);
    const char *nm = c->chunk->functions[fid].name;
    if (vm_str_len(nm) <= hl || vm_str_eq(nm, hl, handle, hl) == VM_FALSE || nm[hl] != '.') return VM_FALSE;
    char msg[240];
    const char *parts[] = {"Module ", c->loaded_full[owner], " is not imported here (add 'import ", c->loaded_full[owner],
                           "', or the module that uses it can share it with 'pub import')"};
    vm_u32 k = 0;
    for (int i = 0; i < 5; i++) {
        vm_u32 pl = vm_str_len(parts[i]);
        for (vm_u32 j = 0; j < pl && k + 1 < sizeof(msg); j++) msg[k++] = parts[i][j];
    }
    msg[k] = '\0';
    gen_error(g, at, msg);
    return VM_TRUE;
}

/* Non-pub functions of a module can only be used from inside that module */
static vm_bool module_private_blocked(Gen *g, AstRef at, int fid) {
    Compiler *c = g->c;
    const VMFunctionInfo *fn = &c->chunk->functions[fid];
    int owner = c->func_module[fid];
    if (fn->is_pub == VM_TRUE || owner < 0) return VM_FALSE;
    int caller = (c->current_func_id >= 0) ? c->func_module[c->current_func_id] : c->cur_module_idx;
    if (caller == owner) return VM_FALSE;
    char msg[200];
    const char *nm = fn->name;
    const char *dot = nm;
    for (const char *q = nm; *q; q++) if (*q == '.') dot = q + 1;
    const char *parts[] = {"'", dot, "' is private to module ", c->loaded_full[owner], " (add 'pub' to share it)"};
    vm_u32 n = 0;
    for (int k = 0; k < 5; k++) {
        vm_u32 pl = vm_str_len(parts[k]);
        for (vm_u32 j = 0; j < pl && n + 1 < sizeof(msg); j++) msg[n++] = parts[k][j];
    }
    msg[n] = '\0';
    gen_error(g, at, msg);
    return VM_TRUE;
}

static void name_of(Gen *g, vm_u32 tok, const char **s, vm_u32 *len) {
    const Token *k = gen_tok(g, tok);
    if (k->type == TOK_SELF_TYPE && g->c->current_struct_id >= 0) {
        *s = g->c->structs[g->c->current_struct_id].name;
        *len = vm_str_len(*s);
        return;
    }
    *s = k->start;
    *len = k->length;
}

static ExprResult result_from_local(Gen *g, int loc) {
    Compiler *c = g->c;
    ExprResult res = gen_result(c->locals[loc].type);
    res.reg = gen_local_value(g, loc);
    res.is_closure = c->locals[loc].is_closure;
    res.is_const = c->locals[loc].is_const;
    res.const_val = c->locals[loc].const_val;
    res.elem_count = c->locals[loc].elem_count;
    for (vm_u8 k = 0; k < res.elem_count; k++) res.elem_types[k] = c->locals[loc].elem_types[k];
    res.struct_id = c->locals[loc].struct_id;
    res.lambda_func_id = c->locals[loc].lambda_func_id;
    res.lambda_gen_id = c->locals[loc].lambda_gen_id;
    if (c->locals[loc].func_type_id >= 0 && res.lambda_gen_id < 0) EXPR_SET_FUNC_TYPE(res, c->locals[loc].func_type_id);
    return res;
}

static ExprResult load_global_const(Gen *g, int gconst) {
    Compiler *c = g->c;
    ExprResult res = gen_result(c->global_consts[gconst].type);
    res.is_const = VM_TRUE;
    res.const_val = c->global_consts[gconst].val;
    res.reg = c_emit_load_const(c, res.const_val, res.type);
    return res;
}

static ExprResult bool_result(vm_u8 reg) {
    ExprResult r = gen_result(VM_TYPE_BOOL);
    r.reg = reg;
    return r;
}

static VMOpCode arith_op(TokenType op, VMDataType t) {
    switch (op) {
        case TOK_PLUS: case TOK_PLUS_EQ:
            return (t == VM_TYPE_F32) ? OP_ADD_F32 : (t == VM_TYPE_F64) ? OP_ADD_F64 : (t == VM_TYPE_I64) ? OP_ADD_I64 : OP_ADD_I32;
        case TOK_MINUS: case TOK_MINUS_EQ:
            return (t == VM_TYPE_F32) ? OP_SUB_F32 : (t == VM_TYPE_F64) ? OP_SUB_F64 : (t == VM_TYPE_I64) ? OP_SUB_I64 : OP_SUB_I32;
        case TOK_STAR: case TOK_STAR_EQ:
            return (t == VM_TYPE_F32) ? OP_MUL_F32 : (t == VM_TYPE_F64) ? OP_MUL_F64 : (t == VM_TYPE_I64) ? OP_MUL_I64 : OP_MUL_I32;
        case TOK_SLASH: case TOK_SLASH_EQ:
            return (t == VM_TYPE_F32) ? OP_DIV_F32 : (t == VM_TYPE_F64) ? OP_DIV_F64 : (t == VM_TYPE_I64) ? OP_DIV_I64 : OP_DIV_I32;
        case TOK_PERCENT: case TOK_MOD_EQ: return (t == VM_TYPE_I64) ? OP_MOD_I64 : OP_MOD_I32;
        case TOK_BAND: case TOK_BAND_EQ:   return (t == VM_TYPE_I64) ? OP_BAND_I64 : OP_BAND_I32;
        case TOK_BOR: case TOK_BOR_EQ:     return (t == VM_TYPE_I64) ? OP_BOR_I64 : OP_BOR_I32;
        case TOK_BXOR: case TOK_BXOR_EQ:   return (t == VM_TYPE_I64) ? OP_BXOR_I64 : OP_BXOR_I32;
        case TOK_SHL: case TOK_SHL_EQ:     return (t == VM_TYPE_I64) ? OP_SHL_I64 : OP_SHL_I32;
        default:                           return (t == VM_TYPE_I64) ? OP_SHR_I64 : OP_SHR_I32;
    }
}

static const char *op_method_name(TokenType op) {
    switch (op) {
        case TOK_PLUS: case TOK_PLUS_EQ:     return "op+";
        case TOK_MINUS: case TOK_MINUS_EQ:   return "op-";
        case TOK_STAR: case TOK_STAR_EQ:     return "op*";
        case TOK_SLASH: case TOK_SLASH_EQ:   return "op/";
        case TOK_PERCENT: case TOK_MOD_EQ:   return "op%";
        case TOK_BAND: case TOK_BAND_EQ:     return "op&";
        case TOK_BOR: case TOK_BOR_EQ:       return "op|";
        case TOK_BXOR: case TOK_BXOR_EQ:     return "op^";
        case TOK_SHL: case TOK_SHL_EQ:       return "op<<";
        case TOK_SHR: case TOK_SHR_EQ:       return "op>>";
        case TOK_EQ:                         return "op==";
        case TOK_NE:                         return "op!=";
        case TOK_LT:                         return "op<";
        case TOK_LE:                         return "op<=";
        case TOK_GT:                         return "op>";
        default:                             return "op>=";
    }
}

/* Calls a struct operator method `op_name(left, right)` */
static ExprResult struct_binop(Gen *g, AstRef at, ExprResult left, ExprResult right, const char *op_name) {
    Compiler *c = g->c;
    VMDataType arg_types[2] = {left.type, right.type};
    int arg_sids[2] = {left.struct_id, right.struct_id};
    int fid = c_resolve_struct_method_overload(c, left.struct_id, op_name, vm_str_len(op_name), arg_types, arg_sids, 2);
    if (fid < 0) {
        gen_error(g, at, "Struct does not define this overloaded operator");
        return gen_result(VM_TYPE_I32);
    }
    ExprResult args[2] = {left, right};
    return gen_call_fid(g, fid, args, 2);
}

static ExprResult struct_unop(Gen *g, AstRef at, ExprResult inner, const char *op_name, const char *err_msg) {
    Compiler *c = g->c;
    VMDataType arg_types[1] = {inner.type};
    int arg_sids[1] = {inner.struct_id};
    int fid = c_resolve_struct_method_overload(c, inner.struct_id, op_name, vm_str_len(op_name), arg_types, arg_sids, 1);
    if (fid < 0) {
        gen_error(g, at, err_msg);
        return gen_result(VM_TYPE_I32);
    }
    return gen_call_fid(g, fid, &inner, 1);
}

static ExprResult not_bool(Gen *g, ExprResult r) {
    vm_u8 dst = c_alloc_reg(g->c);
    vm_chunk_emit(g->c->chunk, VM_MAKE_ABC(OP_NOT_BOOL, dst, r.reg, 0));
    r.reg = dst;
    return r;
}

static ExprResult length_of(Gen *g, AstRef at, ExprResult arg) {
    if (arg.struct_id >= 0) {
        return struct_unop(g, at, arg, "op_len", "'#' length operator requires a string, array, map, or struct defining 'operator #' / 'operator len'");
    }
    vm_u8 dst = c_alloc_reg(g->c);
    VMOpCode len_op = (arg.type == VM_TYPE_ARRAY) ? OP_ARR_LEN : (arg.type == VM_TYPE_MAP) ? OP_MAP_LEN : OP_STRLEN;
    vm_chunk_emit(g->c->chunk, VM_MAKE_ABC(len_op, dst, arg.reg, 0));
    ExprResult res = gen_result(VM_TYPE_I32);
    res.reg = dst;
    return res;
}

/* ---------------------------------------------------------------------------
 * Literals
 * ------------------------------------------------------------------------- */

static ExprResult gen_int(Gen *g, AstRef n, VMDataType expected) {
    vm_i64 v = gen_tok(g, ND(g, n)->tok)->int_val;
    ExprResult res = gen_result(VM_TYPE_I32);
    res.is_const = VM_TRUE;
    /* Typed by context; with no numeric expectation an integer literal is an `int` (i64) */
    vm_bool fits_i32 = vm_bool_from_int(v >= -2147483648LL && v <= 2147483647LL);
    if (expected == VM_TYPE_I32 && fits_i32) { res.type = VM_TYPE_I32; res.const_val.i32 = (vm_i32)v; }
    else if (expected == VM_TYPE_F32) { res.type = VM_TYPE_F32; res.const_val.f32 = (vm_f32)v; }
    else if (expected == VM_TYPE_F64) { res.type = VM_TYPE_F64; res.const_val.f64 = (vm_f64)v; }
    else                              { res.type = VM_TYPE_I64; res.const_val.i64 = v; }
    res.elem_types[0] = res.type;
    res.reg = c_emit_load_const(g->c, res.const_val, res.type);
    return res;
}

static ExprResult gen_float(Gen *g, AstRef n, VMDataType expected) {
    vm_f64 v = gen_tok(g, ND(g, n)->tok)->float_val;
    ExprResult res = gen_result(VM_TYPE_F32);
    res.is_const = VM_TRUE;
    if (expected == VM_TYPE_F64) { res.type = VM_TYPE_F64; res.const_val.f64 = v; }
    else                         { res.type = VM_TYPE_F32; res.const_val.f32 = (vm_f32)v; }
    res.elem_types[0] = res.type;
    res.reg = c_emit_load_const(g->c, res.const_val, res.type);
    return res;
}

/*
 * Decodes one escape at src[i] (src[i] == '\\'); writes the byte to *out, returns chars consumed.
 * \n \t \r \0 \\ \" \' \{ \} \xHH; any other `\c` is kept as the two characters.
 */
static vm_u32 decode_escape(const char *src, vm_u32 i, vm_u32 len, char *out, vm_bool *two_chars) {
    *two_chars = VM_FALSE;
    if (i + 1 >= len) { *out = '\\'; return 1; }
    char e = src[i + 1];
    switch (e) {
        case 'n':  *out = '\n'; return 2;
        case 't':  *out = '\t'; return 2;
        case 'r':  *out = '\r'; return 2;
        case '0':  *out = '\0'; return 2;
        case '\\': *out = '\\'; return 2;
        case '"':  *out = '"';  return 2;
        case '\'': *out = '\''; return 2;
        case '{':  *out = '{';  return 2;
        case '}':  *out = '}';  return 2;
        case 'x': {
            int v = 0, d = 0;
            for (vm_u32 k = i + 2; k < len && d < 2; k++, d++) {
                char h = src[k];
                int hv = (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                if (hv < 0) break;
                v = v * 16 + hv;
            }
            if (d == 0) break;
            *out = (char)v;
            return 2 + (vm_u32)d;
        }
        default: break;
    }
    *out = '\\';
    *two_chars = VM_TRUE;
    return 1;
}

/* Interns a string literal after decoding its escapes */
static vm_u32 literal_string(Gen *g, const char *src, vm_u32 len) {
    vm_bool has_escape = VM_FALSE;
    for (vm_u32 i = 0; i < len; i++) if (src[i] == '\\') has_escape = VM_TRUE;
    if (!has_escape) return gen_const_string(g, src, len);
    char stack[256];
    char *buf = (len < sizeof(stack)) ? stack : (char *)g->c->chunk->realloc_fn(g->c->chunk->alloc_ud, VM_NULL, 0, len + 1);
    if (buf == VM_NULL) return gen_const_string(g, src, len);
    vm_u32 out = 0;
    for (vm_u32 i = 0; i < len;) {
        if (src[i] == '\\') {
            vm_bool two;
            char ch;
            vm_u32 used = decode_escape(src, i, len, &ch, &two);
            buf[out++] = ch;
            i += used;
            continue;
        }
        buf[out++] = src[i++];
    }
    vm_u32 sid = gen_const_string(g, buf, out);
    if (buf != stack) g->c->chunk->realloc_fn(g->c->chunk->alloc_ud, buf, len + 1, 0);
    return sid;
}

static ExprResult gen_string(Gen *g, AstRef n) {
    const Token *k = gen_tok(g, ND(g, n)->tok);
    ExprResult res = gen_result(VM_TYPE_STRING);
    res.is_const = VM_TRUE;
    res.const_val.str_id = literal_string(g, k->start, k->length);
    res.reg = c_emit_load_const(g->c, res.const_val, VM_TYPE_STRING);
    return res;
}

static void fstring_append(Gen *g, vm_u8 part_reg, vm_bool *has_acc, vm_u8 *acc_reg) {
    if (*has_acc == VM_FALSE) {
        *acc_reg = part_reg;
        *has_acc = VM_TRUE;
    } else {
        vm_u8 dst = c_alloc_reg(g->c);
        vm_chunk_emit(g->c->chunk, VM_MAKE_ABC(OP_STRCAT, dst, *acc_reg, part_reg));
        *acc_reg = dst;
    }
}

/* Literal run of an f-string: escapes decoded, `{{` -> {, `}}` -> } */
static void fstring_literal(Gen *g, const char *src, vm_u32 len, vm_bool *has_acc, vm_u8 *acc_reg) {
    if (len == 0) return;
    char *dst = vm_chunk_arena_reserve(g->c->chunk, len);
    if (dst == VM_NULL) return;
    vm_u32 out = 0;
    for (vm_u32 i = 0; i < len; i++) {
        if (src[i] == '\\') {
            vm_bool two;
            char ch;
            i += decode_escape(src, i, len, &ch, &two) - 1;
            dst[out++] = ch;
            continue;
        }
        if ((src[i] == '{' || src[i] == '}') && i + 1 < len && src[i + 1] == src[i]) { dst[out++] = src[i++]; continue; }
        dst[out++] = src[i];
    }
    if (out == 0) return;
    dst[out] = '\0';
    g->c->chunk->string_arena_used += out + 1;
    VMReg kval = {0};
    kval.str_id = vm_chunk_add_string(g->c->chunk, dst, out);
    vm_u8 sreg = c_emit_load_const(g->c, kval, VM_TYPE_STRING);
    fstring_append(g, sreg, has_acc, acc_reg);
}

static ExprResult gen_fstring(Gen *g, AstRef n) {
    const Token *ft = gen_tok(g, ND(g, n)->tok);
    AstList parts = ND(g, n)->a;
    vm_bool has_acc = VM_FALSE;
    vm_u8 acc_reg = 0;
    for (vm_u32 i = 0; i < LLEN(g, parts); i++) {
        AstRef p = LAT(g, parts, i);
        if (ND(g, p)->kind == AST_STR_PART) {
            fstring_literal(g, ft->start + ND(g, p)->a, ND(g, p)->b, &has_acc, &acc_reg);
            continue;
        }
        ExprResult sub = gen_expr(g, p, VM_TYPE_STRING);
        vm_u8 sreg = gen_to_string_reg(g, sub);
        fstring_append(g, sreg, &has_acc, &acc_reg);
    }
    if (has_acc == VM_FALSE) {
        VMReg empty = {0};
        empty.str_id = vm_chunk_add_string(g->c->chunk, "", 0);
        acc_reg = c_emit_load_const(g->c, empty, VM_TYPE_STRING);
    }
    ExprResult res = gen_result(VM_TYPE_STRING);
    res.reg = acc_reg;
    return res;
}

static ExprResult gen_array(Gen *g, AstRef n) {
    Compiler *c = g->c;
    vm_u8 arr_reg = c_alloc_reg(c);
    VMDataType elem_type = VM_TYPE_I32;
    vm_u32 patch_ip = vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_NEW, arr_reg, (vm_u8)elem_type, 0));
    AstList items = ND(g, n)->a;
    VMDataType hint = g->elem_hint;
    g->elem_hint = GEN_ANY;
    for (vm_u32 i = 0; i < LLEN(g, items); i++) {
        ExprResult e = gen_expr(g, LAT(g, items, i), (i == 0) ? hint : elem_type);
        if (i == 0 && hint != GEN_ANY) e = gen_convert(g, e, hint);
        if (i > 0) e = gen_convert(g, e, elem_type);
        if (i == 0) {
            elem_type = e.type;
            c->chunk->code[patch_ip] = VM_MAKE_ABC(OP_ARR_NEW, arr_reg, (vm_u8)elem_type, 0);
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_PUSH, arr_reg, e.reg, 0));
    }
    if (LLEN(g, items) == 0) {
        /* Unknown element type at runtime: the GC scans it conservatively (a `T[]` annotation patches it) */
        c->chunk->code[patch_ip] = VM_MAKE_ABC(OP_ARR_NEW, arr_reg, (vm_u8)VM_TYPE_VOID, 0);
        c->last_empty_array_ip = patch_ip;
    }
    ExprResult res = gen_result(VM_TYPE_ARRAY);
    res.reg = arr_reg;
    res.elem_types[0] = elem_type;
    return res;
}

static ExprResult gen_map(Gen *g, AstRef n) {
    Compiler *c = g->c;
    vm_u8 map_reg = c_alloc_reg(c);
    VMDataType val_type = VM_TYPE_I32;
    vm_u32 patch_ip = vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_NEW, map_reg, (vm_u8)VM_TYPE_STRING, (vm_u8)val_type));
    AstList keys = ND(g, n)->a;
    AstList vals = ND(g, n)->b;
    for (vm_u32 i = 0; i < LLEN(g, keys); i++) {
        ExprResult k = gen_expr(g, LAT(g, keys, i), VM_TYPE_STRING);
        ExprResult v = gen_expr(g, LAT(g, vals, i), (i == 0) ? GEN_ANY : val_type);
        if (i > 0) v = gen_convert(g, v, val_type);
        if (i == 0) {
            val_type = v.type;
            c->chunk->code[patch_ip] = VM_MAKE_ABC(OP_MAP_NEW, map_reg, (vm_u8)VM_TYPE_STRING, (vm_u8)val_type);
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_SET, map_reg, k.reg, v.reg));
    }
    if (LLEN(g, keys) == 0) {
        c->chunk->code[patch_ip] = VM_MAKE_ABC(OP_MAP_NEW, map_reg, (vm_u8)VM_TYPE_STRING, (vm_u8)VM_TYPE_VOID);
    }
    ExprResult res = gen_result(VM_TYPE_MAP);
    res.reg = map_reg;
    res.elem_types[0] = val_type;
    return res;
}

/* `(a, b, c)`: values in consecutive registers */
static ExprResult gen_tuple(Gen *g, AstRef n, VMDataType expected) {
    Compiler *c = g->c;
    AstList items = ND(g, n)->a;
    vm_u32 count = LLEN(g, items);
    vm_u8 base = c_alloc_reg(c);
    for (vm_u32 i = 1; i < count; i++) c_alloc_reg(c);
    ExprResult res = gen_result(VM_TYPE_I32);
    res.reg = base;
    res.elem_count = 0;
    for (vm_u32 i = 0; i < count && i < MAX_TUPLE_ELEMS; i++) {
        vm_u8 save = c->next_reg;
        ExprResult e = gen_expr(g, LAT(g, items, i), (i == 0) ? expected : GEN_ANY);
        c->next_reg = save;
        if (e.reg != (vm_u8)(base + i)) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(base + i), e.reg, 0));
        if (i == 0) res.type = e.type;
        res.elem_types[res.elem_count++] = e.type;
    }
    return res;
}

/* ---------------------------------------------------------------------------
 * Identifiers & members
 * ------------------------------------------------------------------------- */

static ExprResult gen_ident(Gen *g, AstRef n) {
    Compiler *c = g->c;
    vm_u32 tok = ND(g, n)->tok;
    const char *name;
    vm_u32 len;
    name_of(g, tok, &name, &len);

    int loc = c_resolve_local(c, name, len);
    if (loc >= 0) return result_from_local(g, loc);

    int up = gen_resolve_upvalue(g, name, len);
    if (up >= 0) return gen_upvalue_get(g, up);

    /* Struct values cannot be captured by closures yet: read the live enclosing frame (legacy) */
    int oloc = c_resolve_outer_local(c, name, len);
    if (oloc >= 0) {
        LocalVar *ov = &c->outer_locals[oloc];
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_GET_OUTER, dst, ov->reg, 1));
        for (vm_u8 k = 1; k < ov->elem_count; k++) {
            vm_u8 edst = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_GET_OUTER, edst, (vm_u8)(ov->reg + k), 1));
        }
        ExprResult res = gen_result(ov->type);
        res.reg = dst;
        res.elem_count = ov->elem_count;
        for (vm_u8 k = 0; k < res.elem_count; k++) res.elem_types[k] = ov->elem_types[k];
        res.struct_id = ov->struct_id;
        res.lambda_func_id = ov->lambda_func_id;
        return res;
    }

    int gconst = c_resolve_global_const(c, name, len);
    if (gconst < 0 && c->module_prefix[0] != '\0') {
        /* inside a module, a bare name also finds the module's own constants */
        char mq[64];
        vm_str_join_dot(mq, 64, c->module_prefix, vm_str_len(c->module_prefix), name, len);
        gconst = c_resolve_global_const(c, mq, vm_str_len(mq));
    }
    if (gconst >= 0) return load_global_const(g, gconst);

    if (vm_str_eq_cstr(name, len, "none")) return gen_none(g);

    int fn_ref = c_resolve_func(c, name, len);
    if (fn_ref >= 0) {
        if (module_private_blocked(g, n, fn_ref)) return gen_result(VM_TYPE_I32);
        if (module_not_imported(g, n, fn_ref)) return gen_result(VM_TYPE_I32);
        ExprResult res = gen_result(VM_TYPE_I32);
        res.reg = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, res.reg, (vm_i16)fn_ref));
        res.lambda_func_id = fn_ref;
        return res;
    }

    gen_error(g, n, "Undefined variable or identifier");
    return gen_result(VM_TYPE_I32);
}

/* For `a.b` / `a.b.c`: the namespace token (`a` / `b`) and member token, or 0 if `obj` is not a plain path */
static vm_bool member_path(Gen *g, AstRef fg, vm_u32 *ns_tok, vm_u32 *member_tok) {
    AstRef obj = ND(g, fg)->a;
    *member_tok = ND(g, fg)->tok;
    if (ND(g, obj)->kind == AST_IDENT) {
        *ns_tok = ND(g, obj)->tok;
        return VM_TRUE;
    }
    if (ND(g, obj)->kind == AST_FIELD_GET && ND(g, ND(g, obj)->a)->kind == AST_IDENT) {
        *ns_tok = ND(g, obj)->tok;
        return VM_TRUE;
    }
    return VM_FALSE;
}

static int struct_field_index(Compiler *c, int sid, const char *name, vm_u32 len) {
    StructDef *sd = &c->structs[sid];
    for (vm_u8 f = 0; f < sd->num_fields; f++) {
        if (vm_str_eq_cstr(name, len, sd->field_names[f]) == VM_TRUE) return f;
    }
    return -1;
}

static ExprResult struct_field_of(Gen *g, AstRef at, ExprResult obj, const char *mname, vm_u32 mlen) {
    Compiler *c = g->c;
    int f = struct_field_index(c, obj.struct_id, mname, mlen);
    if (f < 0) {
        gen_error(g, at, "Unknown member or field");
        return gen_result(VM_TYPE_I32);
    }
    StructDef *sd = &c->structs[obj.struct_id];
    if (c_can_access_struct_member(c, obj.struct_id, sd->field_is_pub[f]) == VM_FALSE) {
        gen_error(g, at, "Cannot access private struct field (missing 'pub') from outside its struct");
    }
    ExprResult res = gen_result(sd->field_types[f]);
    res.reg = (vm_u8)(obj.reg + f);
    return res;
}

static ExprResult gen_field_get(Gen *g, AstRef n) {
    Compiler *c = g->c;
    vm_u32 ns_tok, member_tok;
    const char *mname;
    vm_u32 mlen;
    name_of(g, ND(g, n)->tok, &mname, &mlen);

    if (member_path(g, n, &ns_tok, &member_tok) == VM_TRUE) {
        const char *ns;
        vm_u32 nslen;
        name_of(g, ns_tok, &ns, &nslen);
        char qname[64];
        vm_str_join_dot(qname, 64, ns, nslen, mname, mlen);
        vm_u32 qlen = vm_str_len(qname);

        int ev = c_resolve_enum_variant(c, qname, qlen);
        if (ev >= 0 && c->enum_variants[ev].enum_sid >= 0) return gen_enum_value(g, n, ev, 0);
        int gconst = c_resolve_global_const(c, qname, qlen);
        if (gconst >= 0) return load_global_const(g, gconst);

        int loc = c_resolve_local(c, ns, nslen);
        if (ND(g, ND(g, n)->a)->kind == AST_IDENT && loc >= 0 && c->locals[loc].struct_id >= 0) {
            return struct_field_of(g, n, result_from_local(g, loc), mname, mlen);
        }
    }

    /* General case: field of any struct-valued expression */
    ExprResult obj = gen_expr(g, ND(g, n)->a, VM_TYPE_I32);
    if (obj.struct_id >= 0) return struct_field_of(g, n, obj, mname, mlen);
    gen_error(g, n, "Unknown member or field");
    return gen_result(VM_TYPE_I32);
}

/* ---------------------------------------------------------------------------
 * Calls
 * ------------------------------------------------------------------------- */

/*
 * Compiles arguments and calls the best overload of `qname` (free function), or of
 * method `mname` on struct `struct_sid`, or `fixed_fid` when already known.
 */
static ExprResult overloaded_call_ex(Gen *g, AstRef at, const char *qname, vm_u32 qlen, int fixed_fid,
                                     int struct_sid, const char *mname, vm_u32 mlen,
                                     const ExprResult *self_arg, AstList arg_list, int fn_reg);

static ExprResult overloaded_call(Gen *g, AstRef at, const char *qname, vm_u32 qlen, int fixed_fid,
                                  int struct_sid, const char *mname, vm_u32 mlen,
                                  const ExprResult *self_arg, AstList arg_list) {
    return overloaded_call_ex(g, at, qname, qlen, fixed_fid, struct_sid, mname, mlen, self_arg, arg_list, -1);
}

/* Calls a known function `fid` through the closure value in `fn_reg` */
static ExprResult closure_call(Gen *g, AstRef at, int fid, vm_u8 fn_reg, AstList arg_list) {
    return overloaded_call_ex(g, at, "", 0, fid, -1, "", 0, VM_NULL, arg_list, fn_reg);
}

static vm_bool is_block_arg(Gen *g, AstRef n) {
    return vm_bool_from_int(ND(g, n)->kind == AST_FUNC && (ND(g, n)->flags & AST_F_BLOCK));
}

/* Compiles a trailing block for parameter `pidx` of `fid` (its callback type gives the block's types) */
static ExprResult block_arg(Gen *g, AstRef blk, int fid, vm_u8 pidx) {
    g->block_ftid = (fid >= 0 && pidx < VM_MAX_PARAMS) ? g->c->chunk->functions[fid].param_func_type[pidx] : -1;
    ExprResult r = gen_expr(g, blk, VM_TYPE_I32);
    g->block_ftid = -1;
    return r;
}

/* Candidate functions for a call: struct methods `Struct.m`, free functions / aliases named `qname` */
static int call_candidates(Gen *g, const char *qname, vm_u32 qlen, int fixed_fid, int struct_sid,
                           const char *mname, vm_u32 mlen, int *out, int max) {
    Compiler *c = g->c;
    int n = 0;
    if (fixed_fid >= 0) {
        out[n++] = fixed_fid;
        return n;
    }
    char sq[64];
    const char *want = qname;
    vm_u32 wlen = qlen;
    if (struct_sid >= 0) {
        vm_str_join_dot(sq, 64, c->structs[struct_sid].name, vm_str_len(c->structs[struct_sid].name), mname, mlen);
        want = sq;
        wlen = vm_str_len(sq);
    }
    for (vm_u32 f = 0; f < c->chunk->num_functions && n < max; f++) {
        if (vm_str_eq(c->chunk->functions[f].name, vm_str_len(c->chunk->functions[f].name), want, wlen) == VM_TRUE) out[n++] = (int)f;
    }
    for (int a = 0; a < c->num_func_aliases && n < max; a++) {
        if (vm_str_eq(c->func_aliases[a].alias_name, vm_str_len(c->func_aliases[a].alias_name), want, wlen) == VM_TRUE) {
            out[n++] = c->func_aliases[a].func_id;
        }
    }
    return n;
}

/* `f { a = 1, b = 2 }`: match names to parameters; unnamed parameters use their defaults */
static ExprResult named_call(Gen *g, AstRef at, const char *qname, vm_u32 qlen, int fixed_fid,
                             int struct_sid, const char *mname, vm_u32 mlen,
                             const ExprResult *self_arg, AstList arg_list, int fn_reg) {
    Compiler *c = g->c;
    int cands[32];
    int nc = call_candidates(g, qname, qlen, fixed_fid, struct_sid, mname, mlen, cands, 32);
    vm_u8 first = (self_arg != VM_NULL) ? 1 : 0;
    int fid = -1;
    for (int k = 0; k < nc && fid < 0; k++) {
        VMFunctionInfo *fn = &c->chunk->functions[cands[k]];
        vm_bool ok = VM_TRUE;
        for (vm_u32 i = 0; i < LLEN(g, arg_list) && ok; i++) {
            const Token *an = gen_tok(g, ND(g, LAT(g, arg_list, i))->tok);
            vm_bool found = VM_FALSE;
            for (vm_u8 p = first; p < fn->arity && p < VM_MAX_PARAMS; p++) {
                if (g->param_name[cands[k]][p] != VM_NULL &&
                    vm_str_eq(g->param_name[cands[k]][p], g->param_len[cands[k]][p], an->start, an->length) == VM_TRUE) {
                    found = VM_TRUE;
                    break;
                }
            }
            ok = found;
        }
        if (ok) fid = cands[k];
    }
    if (fid < 0) {
        gen_error(g, at, "No function has parameters with these names");
        return gen_result(VM_TYPE_I32);
    }

    VMFunctionInfo *fn = &c->chunk->functions[fid];
    ExprResult args[16];
    vm_u8 argc = 0;
    if (self_arg != VM_NULL) args[argc++] = *self_arg;
    for (vm_u8 p = first; p < fn->arity && p < 16; p++) {
        AstRef value = 0;
        for (vm_u32 i = 0; i < LLEN(g, arg_list); i++) {
            AstRef na = LAT(g, arg_list, i);
            const Token *an = gen_tok(g, ND(g, na)->tok);
            if (vm_str_eq(g->param_name[fid][p], g->param_len[fid][p], an->start, an->length) == VM_TRUE) {
                value = ND(g, na)->a;
                break;
            }
        }
        if (value != 0) {
            args[argc] = is_block_arg(g, value) ? block_arg(g, value, fid, p) : gen_expr(g, value, fn->param_types[p]);
        } else if (fn->param_has_default[p]) {
            args[argc] = gen_result(fn->param_types[p]);
            args[argc].reg = c_emit_load_const(c, fn->param_defaults[p], fn->param_types[p]);
        } else {
            char msg[96];
            gen_quoted_msg(msg, 96, "Missing named argument", g->param_name[fid][p], g->param_len[fid][p]);
            gen_error(g, at, msg);
            return gen_result(VM_TYPE_I32);
        }
        fn = &c->chunk->functions[fid];
        argc++;
    }
    gen_at(g, at);
    return gen_call_value(g, fid, args, argc, fn_reg);
}

static ExprResult overloaded_call_ex(Gen *g, AstRef at, const char *qname, vm_u32 qlen, int fixed_fid,
                                     int struct_sid, const char *mname, vm_u32 mlen,
                                     const ExprResult *self_arg, AstList arg_list, int fn_reg) {
    Compiler *c = g->c;
    if (LLEN(g, arg_list) > 0 && ND(g, LAT(g, arg_list, 0))->kind == AST_NAMED_ARG) {
        return named_call(g, at, qname, qlen, fixed_fid, struct_sid, mname, mlen, self_arg, arg_list, fn_reg);
    }
    ExprResult args[16];
    VMDataType arg_types[16];
    int arg_sids[16];
    AstRef pending_block[16];
    vm_u8 argc = 0;
    if (self_arg != VM_NULL) {
        args[0] = *self_arg;
        arg_types[0] = self_arg->type;
        arg_sids[0] = self_arg->struct_id;
        pending_block[0] = 0;
        argc = 1;
    }
    for (vm_u32 i = 0; i < LLEN(g, arg_list) && argc < 16; i++) {
        AstRef an = LAT(g, arg_list, i);
        pending_block[argc] = 0;
        if (is_block_arg(g, an)) {
            /* Compiled after overload resolution, when the callback type is known */
            pending_block[argc] = an;
            args[argc] = gen_result(VM_TYPE_I32);
        } else {
            args[argc] = gen_expr(g, an, GEN_ANY);
        }
        arg_types[argc] = args[argc].type;
        arg_sids[argc] = args[argc].struct_id;
        argc++;
    }

    int fid = fixed_fid;
    if (fid < 0 && struct_sid >= 0) {
        fid = c_resolve_struct_method_overload(c, struct_sid, mname, mlen, arg_types, arg_sids, argc);
    }
    if (fid < 0) fid = c_resolve_func_overload(c, qname, qlen, arg_types, arg_sids, argc);
    /* Inside a module, a bare name also finds the module's own functions (`helper` -> `mod.helper`) */
    if (fid < 0 && struct_sid < 0 && qlen > 0 && c->module_prefix[0] != '\0') {
        char mq[64];
        vm_str_join_dot(mq, 64, c->module_prefix, vm_str_len(c->module_prefix), qname, qlen);
        fid = c_resolve_func_overload(c, mq, vm_str_len(mq), arg_types, arg_sids, argc);
    }
    /* Automatic `operator string` when passing a struct to a string function (e.g. io.println) */
    if (fid < 0 && argc == 1 && args[0].struct_id >= 0) {
        int str_fid = c_resolve_struct_method(c, args[0].struct_id, "op_str", 6);
        if (str_fid >= 0) {
            ExprResult s = gen_call_fid(g, str_fid, &args[0], 1);
            args[0] = s;
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
        gen_quoted_msg(err_msg, 128, "No matching function or argument types for call to", target, tlen);
        gen_error(g, at, err_msg);
        return gen_result(VM_TYPE_I32);
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
        gen_error(g, at, "Cannot call private struct method (missing 'pub') from outside its struct");
        return gen_result(VM_TYPE_I32);
    }
    if (module_private_blocked(g, at, fid)) return gen_result(VM_TYPE_I32);
    if (module_not_imported(g, at, fid)) return gen_result(VM_TYPE_I32);
    for (vm_u8 i = 0; i < argc; i++) {
        if (pending_block[i] != 0) args[i] = block_arg(g, pending_block[i], fid, i);
    }
    gen_at(g, at);
    return gen_call_value(g, fid, args, argc, fn_reg);
}

/* `error/warn/trace/debug(msg [, level])` -> OP_TRAP */
static ExprResult gen_trap_call(Gen *g, AstRef call, vm_u8 kind) {
    Compiler *c = g->c;
    AstList args = ND(g, call)->b;
    vm_u8 msg_reg = 0;
    if (LLEN(g, args) > 0) {
        ExprResult msg = gen_expr(g, LAT(g, args, 0), VM_TYPE_STRING);
        msg_reg = gen_to_string_reg(g, msg);
    }
    vm_u8 level_reg;
    if (LLEN(g, args) > 1) {
        level_reg = gen_expr(g, LAT(g, args, 1), VM_TYPE_I32).reg;
    } else {
        VMReg one = {0};
        one.i32 = 1;
        level_reg = c_emit_load_const(c, one, VM_TYPE_I32);
    }
    gen_at(g, call);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_TRAP, kind, msg_reg, level_reg));
    return gen_result(VM_TYPE_I32);
}

static ExprResult gen_assert(Gen *g, AstRef call) {
    Compiler *c = g->c;
    AstList args = ND(g, call)->b;
    ExprResult cond = gen_expr(g, LAT(g, args, 0), VM_TYPE_BOOL);
    vm_u32 jmp_ok = gen_emit_jump(g, OP_JMP_TRUE, cond.reg);
    vm_u8 msg_reg;
    if (LLEN(g, args) > 1) {
        ExprResult msg = gen_expr(g, LAT(g, args, 1), VM_TYPE_STRING);
        msg_reg = gen_to_string_reg(g, msg);
    } else {
        VMReg dmsg = {0};
        dmsg.str_id = vm_chunk_add_string(c->chunk, "Assertion failed", 16);
        msg_reg = c_emit_load_const(c, dmsg, VM_TYPE_STRING);
    }
    vm_u8 level_reg;
    if (LLEN(g, args) > 2) {
        level_reg = gen_expr(g, LAT(g, args, 2), VM_TYPE_I32).reg;
    } else {
        VMReg one = {0};
        one.i32 = 1;
        level_reg = c_emit_load_const(c, one, VM_TYPE_I32);
    }
    gen_at(g, call);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_TRAP, 0, msg_reg, level_reg));
    gen_patch_jump(g, jmp_ok);
    return gen_result(VM_TYPE_I32);
}

/* Calls a function value held in a register (callback typed by a func-type contract) */
static ExprResult call_func_value(Gen *g, AstRef call, vm_u8 fn_reg, int ftid) {
    Compiler *c = g->c;
    TypeContract *ftc = &c->type_contracts[ftid];
    AstList args = ND(g, call)->b;
    ExprResult fargs[8];
    vm_u8 argc = 0;
    for (vm_u32 i = 0; i < LLEN(g, args) && argc < 8; i++) {
        VMDataType exp_t = (argc < ftc->func_arity) ? ftc->func_param_types[argc] : GEN_ANY;
        fargs[argc] = gen_expr(g, LAT(g, args, i), exp_t);
        if (argc < ftc->func_arity && ftc->func_param_sids[argc] < 0) fargs[argc] = gen_convert(g, fargs[argc], exp_t);
        argc++;
    }
    vm_u8 ret_slots = (ftc->func_ret_sid >= 0) ? c->structs[ftc->func_ret_sid].num_fields : 1;
    vm_u8 call_base = c_alloc_reg(c);
    for (vm_u8 r = 1; r < ret_slots; r++) c_alloc_reg(c);
    vm_u8 pcursor = (vm_u8)(call_base + ret_slots);
    for (vm_u8 i = 0; i < argc; i++) {
        vm_u8 cnt = gen_struct_width(g, fargs[i]);
        for (vm_u8 k = 0; k < cnt; k++) {
            c_alloc_reg(c);
            if ((vm_u8)(fargs[i].reg + k) != pcursor) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, pcursor, (vm_u8)(fargs[i].reg + k), 0));
            pcursor++;
        }
    }
    c->next_reg = (vm_u8)(call_base + ret_slots);
    gen_at(g, call);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CALL_REG, call_base, fn_reg, 0));
    ExprResult res = gen_result(ftc->func_ret_type);
    res.reg = call_base;
    res.struct_id = ftc->func_ret_sid;
    res.elem_count = ret_slots;
    return res;
}

static void gen_print_value(Gen *g, ExprResult val) {
    Compiler *c = g->c;
    if (val.is_none) {
        VMReg s = {0};
        s.str_id = gen_const_string(g, "none", 4);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_PRINT_STR, c_emit_load_const(c, s, VM_TYPE_STRING), 0, 0));
        return;
    }
    if (val.struct_id >= 0) {
        vm_u8 sreg = gen_to_string_reg(g, val);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_PRINT_STR, sreg, 0, 0));
        return;
    }
    if (val.type == VM_TYPE_ARRAY || val.type == VM_TYPE_MAP) {
        vm_u8 sreg = c_alloc_reg(c);
        vm_u8 tpair = (vm_u8)(((vm_u8)val.type << 4) | ((vm_u8)VM_TYPE_STRING & 0x0Fu));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CAST, sreg, val.reg, tpair));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_PRINT_STR, sreg, 0, 0));
        return;
    }
    VMOpCode pop = (val.type == VM_TYPE_I64) ? OP_PRINT_I64 :
                   (val.type == VM_TYPE_F32) ? OP_PRINT_F32 :
                   (val.type == VM_TYPE_F64) ? OP_PRINT_F64 :
                   (val.type == VM_TYPE_BOOL) ? OP_PRINT_BOOL :
                   (val.type == VM_TYPE_STRING) ? OP_PRINT_STR : OP_PRINT_I32;
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(pop, val.reg, 0, 0));
}

void gen_print(Gen *g, AstRef arg) {
    ExprResult val = gen_expr(g, arg, GEN_ANY);
    gen_print_value(g, val);
}

/* ---------------------------------------------------------------------------
 * Generic calls
 * ------------------------------------------------------------------------- */

/* Binds type parameters by matching a parameter type node (in the template's tree) against an argument */
static void unify_type(Gen *g, int gen_id, AstRef ty, VMDataType at, int asid, VMDataType aelem,
                       VMDataType *types, int *sids, vm_bool *bound) {
    Compiler *c = g->c;
    if (ty == 0) return;
    GenericTemplate *gt = &c->generics[gen_id];
    const AstNode *nd = &g->t->nodes[ty];
    switch ((AstKind)nd->kind) {
        case AST_TY_NAME: {
            if (ast_list_len(g->t, nd->a) > 0) return;
            const Token *k = gen_tok(g, nd->tok);
            for (vm_u8 i = 0; i < gt->num_type_params; i++) {
                if (vm_str_eq_cstr(k->start, k->length, gt->type_param_names[i]) == VM_FALSE) continue;
                if (!bound[i]) {
                    types[i] = at;
                    sids[i] = asid;
                    bound[i] = VM_TRUE;
                } else if (sids[i] < 0 && asid < 0 && gen_is_numeric(types[i]) && gen_is_numeric(at)) {
                    types[i] = gen_wider(types[i], at);
                }
                return;
            }
            return;
        }
        case AST_TY_ARRAY:
            if (at == VM_TYPE_ARRAY && aelem != VM_TYPE_VOID && aelem != VM_TYPE_ARRAY) {
                unify_type(g, gen_id, nd->a, aelem, -1, VM_TYPE_VOID, types, sids, bound);
            }
            return;
        case AST_TY_OPTIONAL:
            if (asid >= 0 && c->structs[asid].is_optional) {
                unify_type(g, gen_id, nd->a, c->structs[asid].opt_inner, -1, VM_TYPE_VOID, types, sids, bound);
            } else {
                unify_type(g, gen_id, nd->a, at, asid, aelem, types, sids, bound);
            }
            return;
        default:
            return;
    }
}

/* `f(args)` for a generic f: evaluates the arguments, infers T..., then calls the instance */
ExprResult gen_infer_generic_call(Gen *g, AstRef call, int gen_id, AstList args) {
    Compiler *c = g->c;
    GenericTemplate *gt = &c->generics[gen_id];
    vm_u32 argc = LLEN(g, args);
    if (argc > 16) argc = 16;
    ExprResult vals[16];
    for (vm_u32 i = 0; i < argc; i++) {
        AstRef a = LAT(g, args, i);
        if (ND(g, a)->kind == AST_NAMED_ARG || ND(g, a)->kind == AST_FUNC) {
            gen_error(g, call, "Cannot infer generic type arguments here: write them explicitly, e.g. f<int>(...)");
            return gen_result(VM_TYPE_I32);
        }
        vals[i] = gen_expr(g, a, GEN_ANY);
    }
    VMDataType types[8];
    int sids[8];
    vm_bool bound[8] = {VM_FALSE};
    AstTree *saved_t = g->t;
    g->t = g->templates[gen_id].t;
    AstList params = g->t->nodes[g->templates[gen_id].fn].a;
    for (vm_u32 i = 0; i < argc && i < ast_list_len(g->t, params); i++) {
        AstRef pty = g->t->nodes[ast_list_at(g->t, params, i)].a;
        unify_type(g, gen_id, pty, vals[i].type, vals[i].struct_id, vals[i].elem_types[0], types, sids, bound);
    }
    g->t = saved_t;
    for (vm_u8 i = 0; i < gt->num_type_params; i++) {
        if (!bound[i]) {
            char msg[96];
            gen_quoted_msg(msg, sizeof(msg), "Cannot infer type parameter (write it explicitly, e.g. f<int>()):", gt->type_param_names[i], vm_str_len(gt->type_param_names[i]));
            gen_error(g, call, msg);
            return gen_result(VM_TYPE_I32);
        }
    }
    int fid = gen_instantiate_generic(g, gen_id, types, sids, gt->num_type_params, call);
    if (fid < 0) return gen_result(VM_TYPE_I32);
    return gen_call_fid(g, fid, vals, (vm_u8)argc);
}

/* `f<T...>(args)` with explicit type arguments (no inference); args may be empty */
static ExprResult generic_call(Gen *g, AstRef call, int gen_id, AstList targs, AstList args) {
    if (LLEN(g, targs) == 0) return gen_infer_generic_call(g, call, gen_id, args);
    VMDataType types[8];
    int sids[8];
    vm_u8 nt = 0;
    for (vm_u32 i = 0; i < LLEN(g, targs) && nt < 8; i++) {
        sids[nt] = -1;
        types[nt] = gen_type(g, LAT(g, targs, i), &sids[nt], VM_NULL, VM_NULL);
        nt++;
    }
    int fid = gen_instantiate_generic(g, gen_id, types, sids, nt, call);
    if (fid < 0) return gen_result(VM_TYPE_I32);
    return overloaded_call(g, call, "", 0, fid, -1, "", 0, VM_NULL, args);
}

/* `name(...)` */
static ExprResult gen_call_ident(Gen *g, AstRef call, AstRef callee) {
    Compiler *c = g->c;
    vm_u32 tok = ND(g, callee)->tok;
    const Token *k = gen_tok(g, tok);
    AstList args = ND(g, call)->b;

    if (k->type == TOK_PRINT) {
        if (LLEN(g, args) > 0) gen_print(g, LAT(g, args, 0));
        return gen_result(VM_TYPE_VOID);
    }
    if (k->type == TOK_LEN) {
        ExprResult arg = gen_expr(g, LAT(g, args, 0), VM_TYPE_STRING);
        return length_of(g, call, arg);
    }
    if (vm_str_eq_cstr(k->start, k->length, "error") == VM_TRUE) return gen_trap_call(g, call, 0);
    if (vm_str_eq_cstr(k->start, k->length, "panic") == VM_TRUE) return gen_trap_call(g, call, 0);
    if (vm_str_eq_cstr(k->start, k->length, "warn") == VM_TRUE)  return gen_trap_call(g, call, 1);
    if (vm_str_eq_cstr(k->start, k->length, "trace") == VM_TRUE) return gen_trap_call(g, call, 2);
    if (vm_str_eq_cstr(k->start, k->length, "debug") == VM_TRUE) return gen_trap_call(g, call, 3);
    if (vm_str_eq_cstr(k->start, k->length, "assert") == VM_TRUE) return gen_assert(g, call);
    if (vm_str_eq_cstr(k->start, k->length, "pcall") == VM_TRUE || vm_str_eq_cstr(k->start, k->length, "xpcall") == VM_TRUE) {
        gen_error(g, call, "pcall/xpcall were removed: return 'T ! E' and use '?' / 'catch', or write 'f() catch e { ... }' to trap runtime errors");
        return gen_result(VM_TYPE_BOOL);
    }

    const char *name;
    vm_u32 len;
    name_of(g, tok, &name, &len);

    /* Generic call `name<T>(...)` */
    AstList targs = ND(g, call)->c;
    if (LLEN(g, targs) > 0) {
        int gen_id = c_resolve_generic(c, name, len);
        int gloc = c_resolve_local(c, name, len);
        if (gen_id < 0 && gloc >= 0 && c->locals[gloc].lambda_gen_id >= 0) gen_id = c->locals[gloc].lambda_gen_id;
        if (gen_id < 0) {
            gen_error(g, call, "Unknown generic function");
            return gen_result(VM_TYPE_I32);
        }
        return generic_call(g, call, gen_id, targs, args);
    }

    int loc = c_resolve_local(c, name, len);
    if (loc >= 0 && c->locals[loc].lambda_func_id >= 0) {
        if (c->locals[loc].is_closure) return closure_call(g, call, c->locals[loc].lambda_func_id, gen_local_value(g, loc), args);
        return overloaded_call(g, call, "", 0, c->locals[loc].lambda_func_id, -1, "", 0, VM_NULL, args);
    }
    if (loc >= 0 && c->locals[loc].func_type_id >= 0) {
        return call_func_value(g, call, gen_local_value(g, loc), c->locals[loc].func_type_id);
    }
    if (loc < 0) {
        int up = gen_resolve_upvalue(g, name, len);
        if (up >= 0) {
            const LocalVar *o = &g->fns[g->fn_depth - 1].upv[up].origin;
            if (o->lambda_func_id >= 0 && !o->is_closure) {
                return overloaded_call(g, call, "", 0, o->lambda_func_id, -1, "", 0, VM_NULL, args);
            }
            ExprResult fv = gen_upvalue_get(g, up);
            if (o->lambda_func_id >= 0) return closure_call(g, call, o->lambda_func_id, fv.reg, args);
            if (o->func_type_id >= 0) return call_func_value(g, call, fv.reg, o->func_type_id);
            gen_error(g, call, "Captured variable is not callable");
            return gen_result(VM_TYPE_I32);
        }
    }
    if (loc >= 0 && c->locals[loc].struct_id >= 0) {
        ExprResult self_arg = result_from_local(g, loc);
        return overloaded_call(g, call, "", 0, -1, self_arg.struct_id, "op_call", 7, &self_arg, args);
    }
    int oloc = c_resolve_outer_local(c, name, len);
    if (oloc >= 0 && c->outer_locals[oloc].lambda_func_id >= 0) {
        return overloaded_call(g, call, "", 0, c->outer_locals[oloc].lambda_func_id, -1, "", 0, VM_NULL, args);
    }
    char qname[64];
    vm_str_copy(qname, name, len, 64);
    /* `f(x)` where f is generic: type arguments are inferred from the arguments */
    if (c_resolve_func(c, qname, len) < 0) {
        int gen_id = c_resolve_generic(c, name, len);
        if (gen_id < 0 && loc >= 0 && c->locals[loc].lambda_gen_id >= 0) gen_id = c->locals[loc].lambda_gen_id;
        if (gen_id < 0 && c->module_prefix[0] != '\0') {
            char mq[64];
            vm_str_join_dot(mq, 64, c->module_prefix, vm_str_len(c->module_prefix), name, len);
            if (c_resolve_func(c, mq, vm_str_len(mq)) < 0) gen_id = c_resolve_generic(c, mq, vm_str_len(mq));
        }
        if (gen_id >= 0) return gen_infer_generic_call(g, call, gen_id, args);
    }
    return overloaded_call(g, call, qname, len, -1, -1, "", 0, VM_NULL, args);
}

/* `ns.member(...)` */
static ExprResult gen_call_member(Gen *g, AstRef call, AstRef callee) {
    Compiler *c = g->c;
    AstList args = ND(g, call)->b;
    vm_u32 ns_tok, member_tok;
    if (member_path(g, callee, &ns_tok, &member_tok) == VM_FALSE) {
        gen_error(g, call, "Unsupported call target");
        return gen_result(VM_TYPE_I32);
    }
    const char *ns, *member;
    vm_u32 nslen, mlen;
    name_of(g, ns_tok, &ns, &nslen);
    name_of(g, member_tok, &member, &mlen);
    char qname[64];
    vm_str_join_dot(qname, 64, ns, nslen, member, mlen);
    vm_u32 qlen = vm_str_len(qname);
    int ev = c_resolve_enum_variant(c, qname, qlen);

    if (vm_str_eq_cstr(ns, nslen, "io") == VM_TRUE &&
        (vm_str_eq_cstr(member, mlen, "trace") == VM_TRUE || vm_str_eq_cstr(member, mlen, "debug") == VM_TRUE)) {
        return gen_trap_call(g, call, (vm_str_eq_cstr(member, mlen, "trace") == VM_TRUE) ? 2 : 3);
    }
    if (ev >= 0 && c->enum_variants[ev].enum_sid >= 0) return gen_enum_value(g, call, ev, args);
    int sid = c_resolve_struct(c, ns, nslen);
    /* Generic `ns.f<T>(...)` / `ns.f(x)` (module function or struct static method) */
    {
        int gen_id = c_resolve_generic(c, qname, qlen);
        if (gen_id >= 0 && (LLEN(g, ND(g, call)->c) > 0 || c_resolve_func(c, qname, qlen) < 0)) {
            return generic_call(g, call, gen_id, ND(g, call)->c, args);
        }
    }
    return overloaded_call(g, call, qname, qlen, -1, sid, member, mlen, VM_NULL, args);
}

static ExprResult gen_call(Gen *g, AstRef n) {
    AstRef callee = ND(g, n)->a;
    switch (ND(g, callee)->kind) {
        case AST_IDENT:     return gen_call_ident(g, n, callee);
        case AST_FIELD_GET: return gen_call_member(g, n, callee);
        default: break;
    }
    /* Calling the result of an expression (e.g. a returned callback) */
    ExprResult fv = gen_expr(g, callee, VM_TYPE_I32);
    int ftid = EXPR_GET_FUNC_TYPE(fv);
    if (fv.lambda_func_id >= 0) {
        if (fv.is_closure) return closure_call(g, n, fv.lambda_func_id, fv.reg, ND(g, n)->b);
        return overloaded_call(g, n, "", 0, fv.lambda_func_id, -1, "", 0, VM_NULL, ND(g, n)->b);
    }
    if (ftid >= 0) return call_func_value(g, n, fv.reg, ftid);
    gen_error(g, n, "Expression is not callable");
    return gen_result(VM_TYPE_I32);
}

/* `recv:method(...)` on an already evaluated receiver */
ExprResult gen_method_on(Gen *g, AstRef n, ExprResult self_expr, const char *mname, vm_u32 mlen, AstList args) {
    Compiler *c = g->c;
    if (gen_is_optional(g, self_expr)) {
        gen_error(g, n, "Optional value must be unwrapped first (use '?.', '?\?' or 'if let')");
        return self_expr;
    }
    if (self_expr.struct_id >= 0) {
        return overloaded_call(g, n, "", 0, -1, self_expr.struct_id, mname, mlen, &self_expr, args);
    }
    if ((int)self_expr.type >= 0 && (int)self_expr.type < VM_TYPE_COUNT) {
        const char *pname = c->chunk->primitive_struct_name[self_expr.type];
        if (pname[0] != '\0') {
            int psid = c_resolve_struct(c, pname, vm_str_len(pname));
            if (psid >= 0) {
                char sqname[64];
                vm_str_join_dot(sqname, 64, pname, vm_str_len(pname), mname, mlen);
                return overloaded_call(g, n, sqname, vm_str_len(sqname), -1, psid, mname, mlen, &self_expr, args);
            }
        }
    }
    gen_error(g, n, "':' method call requires a struct or primitive type with bound struct");
    return self_expr;
}

static ExprResult gen_method(Gen *g, AstRef n) {
    ExprResult self_expr = gen_expr(g, ND(g, n)->a, GEN_ANY);
    const Token *m = gen_tok(g, ND(g, n)->tok);
    return gen_method_on(g, n, self_expr, m->start, m->length, ND(g, n)->b);
}

/* `obj[index]` */
static ExprResult gen_index(Gen *g, AstRef n) {
    Compiler *c = g->c;
    ExprResult obj = gen_expr(g, ND(g, n)->a, VM_TYPE_I32);
    AstRef idx_node = ND(g, n)->b;

    if (obj.struct_id < 0 && obj.type == VM_TYPE_STRING) {
        ExprResult idx = gen_expr(g, idx_node, VM_TYPE_I32);
        vm_u8 abase = c_alloc_reg(c);
        vm_u8 cnt_reg = c_alloc_reg(c);
        if (idx.reg != abase) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, abase, idx.reg, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, cnt_reg, 1));
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRSUB, dst, obj.reg, abase));
        ExprResult res = gen_result(VM_TYPE_STRING);
        res.reg = dst;
        return res;
    }
    if (obj.struct_id < 0 && obj.type == VM_TYPE_ARRAY) {
        ExprResult idx = gen_expr(g, idx_node, VM_TYPE_I32);
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_GET, dst, obj.reg, idx.reg));
        VMDataType et = obj.elem_types[0];
        if (et == VM_TYPE_ARRAY || et == VM_TYPE_VOID) et = VM_TYPE_I32;
        ExprResult res = gen_result(et);
        res.reg = dst;
        return res;
    }
    if (obj.struct_id < 0 && obj.type == VM_TYPE_MAP) {
        ExprResult key = gen_expr(g, idx_node, VM_TYPE_STRING);
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_GET, dst, obj.reg, key.reg));
        VMDataType vt = obj.elem_types[0];
        if (vt == VM_TYPE_MAP || vt == VM_TYPE_VOID) vt = VM_TYPE_I32;
        ExprResult res = gen_result(vt);
        res.reg = dst;
        return res;
    }
    if (obj.struct_id < 0) {
        gen_error(g, n, "'[]' index operator requires an array, map, string, or struct with 'operator []'");
        return gen_result(VM_TYPE_I32);
    }
    ExprResult key = gen_expr(g, idx_node, GEN_ANY);
    VMDataType arg_types[2] = {obj.type, key.type};
    int arg_sids[2] = {obj.struct_id, key.struct_id};
    int fid = c_resolve_struct_method_overload(c, obj.struct_id, "op[]", 4, arg_types, arg_sids, 2);
    if (fid < 0) {
        gen_error(g, n, "Struct does not define matching 'operator []'");
        return gen_result(VM_TYPE_I32);
    }
    ExprResult args[2] = {obj, key};
    return gen_call_fid(g, fid, args, 2);
}

/* ---------------------------------------------------------------------------
 * Operators
 * ------------------------------------------------------------------------- */

static ExprResult gen_unary(Gen *g, AstRef n, VMDataType expected) {
    Compiler *c = g->c;
    TokenType op = (TokenType)ND(g, n)->op;
    AstRef operand = ND(g, n)->a;
    if (op == TOK_HASH) {
        ExprResult arg = gen_expr(g, operand, VM_TYPE_STRING);
        return length_of(g, n, arg);
    }
    if (op == TOK_MINUS) {
        ExprResult inner = gen_expr(g, operand, expected);
        if (inner.struct_id >= 0) return struct_unop(g, n, inner, "op-", "Struct does not define unary 'operator -'");
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
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(arith_op(TOK_MINUS, inner.type), dst, zreg, inner.reg));
        ExprResult res = gen_result(inner.type);
        res.reg = dst;
        return res;
    }
    if (op == TOK_BNOT) {
        ExprResult inner = gen_expr(g, operand, expected);
        if (inner.struct_id >= 0) return struct_unop(g, n, inner, "op~", "Struct does not define unary 'operator ~'");
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC((inner.type == VM_TYPE_I64) ? OP_BNOT_I64 : OP_BNOT_I32, dst, inner.reg, 0));
        ExprResult res = gen_result((inner.type == VM_TYPE_I64) ? VM_TYPE_I64 : VM_TYPE_I32);
        res.reg = dst;
        return res;
    }
    /* `!` */
    ExprResult inner = gen_expr(g, operand, VM_TYPE_BOOL);
    vm_u8 dst = c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_NOT_BOOL, dst, inner.reg, 0));
    return bool_result(dst);
}

static ExprResult gen_cast(Gen *g, AstRef n, VMDataType expected) {
    Compiler *c = g->c;
    ExprResult left = gen_expr(g, ND(g, n)->a, expected);
    int dst_sid = -1;
    VMDataType dst_type = gen_type(g, ND(g, n)->b, &dst_sid, VM_NULL, VM_NULL);
    if (gen_is_enum(g, left) && dst_sid < 0 && gen_is_numeric(dst_type)) {
        ExprResult tag = gen_result(VM_TYPE_I32);
        tag.reg = left.reg;
        return gen_convert(g, tag, dst_type);
    }
    if (gen_is_enum(g, left) && dst_type == VM_TYPE_STRING) {
        ExprResult s = gen_result(VM_TYPE_STRING);
        s.reg = gen_enum_to_string(g, left);
        return s;
    }
    if (left.struct_id >= 0) {
        char as_name[48];
        if (dst_sid >= 0) {
            const char *sname = c->structs[dst_sid].name;
            vm_u32 slen = vm_str_len(sname);
            vm_str_copy(as_name, "op_as_", 6, 48);
            for (vm_u32 k = 0; k < slen && (6 + k) < 47; k++) as_name[6 + k] = sname[k];
            as_name[6 + (slen < 41 ? slen : 41)] = '\0';
        } else if (dst_type == VM_TYPE_STRING) vm_str_copy(as_name, "op_str", 6, 48);
        else if (dst_type == VM_TYPE_I64)  vm_str_copy(as_name, "op_as_i64", 9, 48);
        else if (dst_type == VM_TYPE_F32)  vm_str_copy(as_name, "op_as_f32", 9, 48);
        else if (dst_type == VM_TYPE_F64)  vm_str_copy(as_name, "op_as_f64", 9, 48);
        else if (dst_type == VM_TYPE_BOOL) vm_str_copy(as_name, "op_as_bool", 10, 48);
        else                               vm_str_copy(as_name, "op_as_i32", 9, 48);
        int as_fid = c_resolve_struct_method(c, left.struct_id, as_name, vm_str_len(as_name));
        if (as_fid < 0) {
            gen_error(g, n, "Struct does not define matching 'operator as' for target type");
            return left;
        }
        return gen_call_fid(g, as_fid, &left, 1);
    }
    vm_u8 dst = c_alloc_reg(c);
    vm_u8 type_pair = (vm_u8)(((vm_u8)left.type << 4) | ((vm_u8)dst_type & 0x0Fu));
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CAST, dst, left.reg, type_pair));
    ExprResult res = gen_result(dst_type);
    res.reg = dst;
    return res;
}

static ExprResult gen_in(Gen *g, AstRef n, VMDataType expected) {
    Compiler *c = g->c;
    ExprResult left = gen_expr(g, ND(g, n)->a, expected);
    ExprResult right = gen_expr(g, ND(g, n)->b, VM_TYPE_STRING);
    if (right.struct_id >= 0) return struct_binop(g, n, right, left, "op_in");
    if (right.type == VM_TYPE_MAP && left.type == VM_TYPE_STRING) {
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_HAS, dst, right.reg, left.reg));
        return bool_result(dst);
    }
    if (right.type == VM_TYPE_ARRAY) {
        int arr_sid = c_resolve_struct(c, "Array", 5);
        VMDataType at[2] = {VM_TYPE_ARRAY, left.type};
        int as[2] = {-1, -1};
        int fid = c_resolve_struct_method_overload(c, arr_sid, "contains", 8, at, as, 2);
        if (fid >= 0) {
            ExprResult args[2] = {right, left};
            ExprResult r = gen_call_fid(g, fid, args, 2);
            r.type = VM_TYPE_BOOL;
            r.struct_id = -1;
            r.elem_count = 1;
            r.elem_types[0] = VM_TYPE_BOOL;
            return r;
        }
    }
    if (right.type == VM_TYPE_STRING && left.type == VM_TYPE_STRING) {
        vm_u8 idx_reg = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRFIND, idx_reg, right.reg, left.reg));
        VMReg zero = {0};
        vm_u8 zreg = c_emit_load_const(c, zero, VM_TYPE_I32);
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_GE_I32, dst, idx_reg, zreg));
        return bool_result(dst);
    }
    gen_error(g, n, "'in' operator requires a string, array, map, or struct defining 'operator in'");
    return left;
}

static ExprResult tag_string(Gen *g, vm_u32 tok) {
    const Token *k = gen_tok(g, tok);
    VMReg kval = {0};
    kval.str_id = gen_const_string(g, k->start, k->length);
    ExprResult tag = gen_result(VM_TYPE_STRING);
    tag.is_const = VM_TRUE;
    tag.const_val = kval;
    tag.reg = c_emit_load_const(g->c, kval, VM_TYPE_STRING);
    return tag;
}

/* `T` used as a value inside a generic instance: its bound type (no register) */
static vm_bool type_param_ref(Gen *g, AstRef n, ExprResult *out) {
    Compiler *c = g->c;
    if (ND(g, n)->kind != AST_IDENT || c->num_active_tparams == 0) return VM_FALSE;
    const Token *k = gen_tok(g, ND(g, n)->tok);
    if (c_resolve_local(c, k->start, k->length) >= 0) return VM_FALSE;
    for (vm_u8 i = 0; i < c->num_active_tparams; i++) {
        if (vm_str_eq_cstr(k->start, k->length, c->active_tparam_names[i]) == VM_FALSE) continue;
        *out = gen_result(c->active_tparam_types[i]);
        out->struct_id = c->active_tparam_sids[i];
        return VM_TRUE;
    }
    return VM_FALSE;
}

static ExprResult gen_is(Gen *g, AstRef n, VMDataType expected) {
    Compiler *c = g->c;
    ExprResult left;
    vm_bool type_only = type_param_ref(g, ND(g, n)->a, &left);
    if (!type_only) left = gen_expr(g, ND(g, n)->a, expected);
    AstRef rhs = ND(g, n)->b;
    int is_fid = (left.struct_id >= 0 && !type_only) ? c_resolve_struct_method(c, left.struct_id, "op_is", 5) : -1;

    if (is_fid >= 0 && ND(g, rhs)->kind == AST_STRING) {
        ExprResult tag = gen_expr(g, rhs, VM_TYPE_STRING);
        return struct_binop(g, n, left, tag, "op_is");
    }

    vm_bool is_match = VM_FALSE;
    vm_u32 target_tok = gen_node_tok(g, rhs);
    const Token *tt = gen_tok(g, target_tok);
    vm_bool rhs_is_ident = vm_bool_from_int(ND(g, rhs)->kind == AST_TY_NAME && tt->type == TOK_IDENT);
    int tid = rhs_is_ident ? c_resolve_type_contract(c, tt->start, tt->length) : -1;

    if (tid >= 0 && c->type_contracts[tid].is_alias == VM_FALSE) {
        is_match = (left.struct_id >= 0) ? c_does_struct_implement_type(c, left.struct_id, tid) : VM_FALSE;
    } else if (is_fid >= 0 && rhs_is_ident && c_resolve_struct(c, tt->start, tt->length) < 0) {
        VMFunctionInfo *fn = &c->chunk->functions[is_fid];
        if (fn->arity == 1) {
            VMDataType at[1] = {left.type};
            int as[1] = {left.struct_id};
            int f1 = c_resolve_struct_method_overload(c, left.struct_id, "op_is", 5, at, as, 1);
            if (f1 >= 0) {
                ExprResult r = gen_call_fid(g, f1, &left, 1);
                r.type = VM_TYPE_BOOL;
                r.struct_id = -1;
                r.elem_count = 1;
                r.elem_types[0] = VM_TYPE_BOOL;
                return r;
            }
        }
        return struct_binop(g, n, left, tag_string(g, target_tok), "op_is");
    } else {
        int check_sid = -1;
        VMDataType check_type = gen_type(g, rhs, &check_sid, VM_NULL, VM_NULL);
        if (check_sid >= 0) is_match = c_is_struct_compatible(c, left.struct_id, check_sid);
        else is_match = vm_bool_from_int(left.type == check_type && left.struct_id < 0);
    }
    if (is_match == VM_FALSE && is_fid >= 0) {
        return struct_binop(g, n, left, tag_string(g, target_tok), "op_is");
    }
    vm_u8 dst = c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LOAD_BOOL, dst, (vm_u8)is_match, 0));
    ExprResult res = bool_result(dst);
    res.is_const = VM_TRUE;
    res.const_val.b = is_match;
    return res;
}

static ExprResult gen_compare(Gen *g, AstRef n, TokenType op, VMDataType expected) {
    Compiler *c = g->c;
    ExprResult left = gen_expr(g, ND(g, n)->a, expected);
    ExprResult right = gen_expr(g, ND(g, n)->b, left.type);
    if (gen_is_result(g, left) || gen_is_result(g, right)) {
        gen_error(g, n, "A 'T ! E' result must be handled with '?' or 'catch' before use");
        return left;
    }
    /* `opt == none` / `opt != none` test the presence flag */
    if ((left.is_none || right.is_none) && (op == TOK_EQ || op == TOK_NE)) {
        ExprResult o = left.is_none ? right : left;
        if (o.is_none) {
            vm_u8 d = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LOAD_BOOL, d, op == TOK_EQ, 0));
            return bool_result(d);
        }
        if (!gen_is_optional(g, o)) {
            gen_error(g, n, "Only optional values (T?) can be compared with 'none'");
            return left;
        }
        if (op == TOK_NE) {
            vm_u8 d = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, d, o.reg, 0));
            return bool_result(d);
        }
        return not_bool(g, bool_result(o.reg));
    }
    if (gen_is_optional(g, left) || gen_is_optional(g, right)) {
        gen_error(g, n, "Optional values can only be compared with 'none' (unwrap with '?\?' or 'if let')");
        return left;
    }
    if (gen_is_enum(g, left) && right.struct_id == left.struct_id && (op == TOK_EQ || op == TOK_NE)) {
        vm_u8 d = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC((op == TOK_EQ) ? OP_EQ_I32 : OP_NE_I32, d, left.reg, right.reg));
        return bool_result(d);
    }
    if (left.struct_id < 0 && right.struct_id < 0 && gen_is_numeric(left.type) && gen_is_numeric(right.type) && left.type != right.type) {
        VMDataType t = gen_wider(left.type, right.type);
        left = gen_convert(g, left, t);
        right = gen_convert(g, right, t);
    }
    if (left.struct_id >= 0) {
        const char *op_name = op_method_name(op);
        VMDataType arg_types[2] = {left.type, right.type};
        int arg_sids[2] = {left.struct_id, right.struct_id};
        int fid = c_resolve_struct_method_overload(c, left.struct_id, op_name, vm_str_len(op_name), arg_types, arg_sids, 2);
        if (fid >= 0) return struct_binop(g, n, left, right, op_name);
        /* Derived comparisons: != from ==, > / <= / >= from < */
        if (op == TOK_NE) return not_bool(g, struct_binop(g, n, left, right, "op=="));
        if (op == TOK_GT && right.struct_id >= 0) return struct_binop(g, n, right, left, "op<");
        if (op == TOK_LE && right.struct_id >= 0) return not_bool(g, struct_binop(g, n, right, left, "op<"));
        if (op == TOK_GE) return not_bool(g, struct_binop(g, n, left, right, "op<"));
        return struct_binop(g, n, left, right, op_name);
    }
    int k = (op == TOK_EQ) ? 0 : (op == TOK_NE) ? 1 : (op == TOK_LT) ? 2 : (op == TOK_LE) ? 3 : (op == TOK_GT) ? 4 : 5;
    VMOpCode base = (left.type == VM_TYPE_STRING) ? OP_EQ_STR :
                    (left.type == VM_TYPE_F32)    ? OP_EQ_F32 :
                    (left.type == VM_TYPE_F64)    ? OP_EQ_F64 :
                    (left.type == VM_TYPE_I64)    ? OP_EQ_I64 : OP_EQ_I32;
    vm_u8 dst = c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC((VMOpCode)(base + k), dst, left.reg, right.reg));
    return bool_result(dst);
}

static ExprResult gen_arith(Gen *g, AstRef n, TokenType op, VMDataType expected) {
    Compiler *c = g->c;
    ExprResult left = gen_expr(g, ND(g, n)->a, expected);
    vm_bool is_bit = vm_bool_from_int(op == TOK_SHL || op == TOK_SHR || op == TOK_BAND || op == TOK_BXOR || op == TOK_BOR);
    vm_u32 before_cnt = c->chunk->code_count;
    vm_u8 before_reg = c->next_reg;
    ExprResult right = gen_expr(g, ND(g, n)->b, left.type);

    if (gen_is_result(g, left) || gen_is_result(g, right)) {
        gen_error(g, n, "A 'T ! E' result must be handled with '?' or 'catch' before use");
        return left;
    }
    if (gen_is_optional(g, left) || gen_is_optional(g, right) || left.is_none || right.is_none) {
        gen_error(g, n, "Optional value must be unwrapped first (use '?\?', 'if let' or '?.')");
        return left;
    }
    if (left.struct_id >= 0) return struct_binop(g, n, left, right, op_method_name(op));
    if (!(op == TOK_PLUS && left.type == VM_TYPE_STRING) && gen_is_numeric(left.type) && gen_is_numeric(right.type) &&
        left.type != right.type) {
        VMDataType t = gen_wider(left.type, right.type);
        if (is_bit && (t == VM_TYPE_F32 || t == VM_TYPE_F64)) t = VM_TYPE_I64;
        left = gen_convert(g, left, t);
        right = gen_convert(g, right, t);
    }

    if (op == TOK_PLUS && left.type == VM_TYPE_STRING) {
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRCAT, dst, left.reg, right.reg));
        ExprResult res = gen_result(VM_TYPE_STRING);
        res.reg = dst;
        return res;
    }
    vm_i64 rimm = (right.type == VM_TYPE_I64) ? right.const_val.i64 : (vm_i64)right.const_val.i32;
    if ((op == TOK_PLUS || op == TOK_MINUS) && left.is_const == VM_FALSE && right.is_const == VM_TRUE &&
        (left.type == VM_TYPE_I32 || left.type == VM_TYPE_I64) && right.type == left.type &&
        rimm >= -127 && rimm <= 127 && c->chunk->code_count == before_cnt + 1) {
        c->chunk->code_count = before_cnt;
        c->next_reg = before_reg;
        vm_i8 imm = (op == TOK_PLUS) ? (vm_i8)rimm : (vm_i8)(-rimm);
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC((left.type == VM_TYPE_I64) ? OP_ADD_IMM_I64 : OP_ADD_IMM_I32, dst, left.reg, (vm_u8)imm));
        ExprResult res = gen_result(left.type);
        res.reg = dst;
        res.elem_types[0] = left.type;
        return res;
    }
    if (is_bit && left.is_const == VM_TRUE && right.is_const == VM_TRUE && left.type == VM_TYPE_I32) {
        vm_i32 a = left.const_val.i32, b = right.const_val.i32;
        vm_i32 v = (op == TOK_SHL) ? (a << (b & 31)) : (op == TOK_SHR) ? (a >> (b & 31)) :
                   (op == TOK_BAND) ? (a & b) : (op == TOK_BXOR) ? (a ^ b) : (a | b);
        left.const_val.i32 = v;
        left.reg = c_emit_load_const(c, left.const_val, VM_TYPE_I32);
        return left;
    }
    vm_u8 dst = c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(arith_op(op, left.type), dst, left.reg, right.reg));
    ExprResult res = gen_result(left.type);
    res.reg = dst;
    return res;
}

static ExprResult gen_logic(Gen *g, AstRef n, TokenType op, VMDataType expected) {
    ExprResult left = gen_expr(g, ND(g, n)->a, expected);
    ExprResult right = gen_expr(g, ND(g, n)->b, VM_TYPE_BOOL);
    vm_u8 dst = c_alloc_reg(g->c);
    vm_chunk_emit(g->c->chunk, VM_MAKE_ABC((op == TOK_LAND) ? OP_AND_BOOL : OP_OR_BOOL, dst, left.reg, right.reg));
    return bool_result(dst);
}

static ExprResult gen_binary(Gen *g, AstRef n, VMDataType expected) {
    TokenType op = (TokenType)ND(g, n)->op;
    switch (op) {
        case TOK_QQ:  return gen_coalesce(g, n, expected);
        case TOK_AS:  return gen_cast(g, n, expected);
        case TOK_IN:  return gen_in(g, n, expected);
        case TOK_IS:  return gen_is(g, n, expected);
        case TOK_EQ: case TOK_NE: case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE:
            return gen_compare(g, n, op, expected);
        case TOK_LAND: case TOK_LOR:
            return gen_logic(g, n, op, expected);
        default:
            return gen_arith(g, n, op, expected);
    }
}

/* ---------------------------------------------------------------------------
 * try / or
 * ------------------------------------------------------------------------- */

static ExprResult gen_try(Gen *g, AstRef n, VMDataType expected) {
    Compiler *c = g->c;
    AstRef body_node = ND(g, n)->a;
    AstRef fb_node = ND(g, n)->b;

    if ((ND(g, n)->flags & AST_F_TRY_KW) == 0) {
        /* `value or fallback`: (ok, payload) tuples pick the payload, otherwise the value itself */
        ExprResult left = gen_expr(g, body_node, expected);
        VMDataType val_type = (left.elem_count >= 2) ? left.elem_types[1] : left.type;
        vm_u8 payload_src = (left.elem_count >= 2) ? (vm_u8)(left.reg + 1) : left.reg;
        vm_u8 dst = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, payload_src, 0));
        vm_u32 jmp_ok = gen_emit_jump(g, OP_JMP_TRUE, left.reg);
        if (fb_node == 0 || ND(g, fb_node)->kind == AST_BLOCK) {
            gen_error(g, n, "'or' fallback block requires 'try'");
            return left;
        }
        ExprResult fb = gen_expr(g, fb_node, val_type);
        if (fb.reg != dst) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, fb.reg, 0));
        gen_patch_jump(g, jmp_ok);
        ExprResult res = gen_result(val_type);
        res.reg = dst;
        return res;
    }

    vm_u8 dst = c_alloc_reg(c);
    vm_u8 err_reg = c_alloc_reg(c);
    vm_u32 try_jmp = gen_emit_jump(g, OP_TRY_BEGIN, err_reg);
    ExprResult body = gen_expr(g, body_node, expected);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_TRY_END, 0, 0, 0));
    if (body.reg != dst) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, body.reg, 0));
    vm_u32 end_jmp = gen_emit_jump(g, OP_JMP, 0);
    gen_patch_jump(g, try_jmp);

    VMDataType fb_type = (body.type != VM_TYPE_VOID) ? body.type : ((expected == GEN_ANY) ? VM_TYPE_VOID : expected);
    if (fb_node != 0 && ND(g, fb_node)->kind == AST_BLOCK) {
        gen_begin_scope(g);
        vm_u32 err_tok = ND(g, n)->tok;
        if (err_tok != 0) {
            LocalVar *lv = &c->locals[c->num_locals++];
            lv->name = gen_tok(g, err_tok)->start;
            lv->length = gen_tok(g, err_tok)->length;
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
        vm_bool saved_active = g->yield_active;
        vm_u8 saved_dst = g->yield_dst;
        VMDataType saved_type = g->yield_type;
        g->yield_active = VM_TRUE;
        g->yield_dst = dst;
        g->yield_type = fb_type;
        gen_block(g, fb_node);
        if (body.type == VM_TYPE_VOID) body.type = g->yield_type;
        g->yield_active = saved_active;
        g->yield_dst = saved_dst;
        g->yield_type = saved_type;
        gen_end_scope(g);
    } else if (fb_node != 0) {
        ExprResult fb = gen_expr(g, fb_node, fb_type);
        if (fb.reg != dst) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, dst, fb.reg, 0));
        if (body.type == VM_TYPE_VOID) body.type = fb.type;
    }
    gen_patch_jump(g, end_jmp);
    body.reg = dst;
    body.is_const = VM_FALSE;
    body.elem_count = 1;
    body.elem_types[0] = body.type;
    return body;
}

/* ---------------------------------------------------------------------------
 * Dispatcher
 * ------------------------------------------------------------------------- */

ExprResult gen_expr(Gen *g, AstRef n, VMDataType expected) {
    if (n == 0 || g->c->had_error) return gen_result(VM_TYPE_I32);
    gen_at(g, n);
    switch ((AstKind)ND(g, n)->kind) {
        case AST_INT:       return gen_int(g, n, expected);
        case AST_FLOAT:     return gen_float(g, n, expected);
        case AST_STRING:    return gen_string(g, n);
        case AST_FSTRING:   return gen_fstring(g, n);
        case AST_BOOL: {
            ExprResult res = gen_result(VM_TYPE_BOOL);
            res.is_const = VM_TRUE;
            res.const_val.b = (vm_bool)ND(g, n)->op;
            res.reg = c_emit_load_const(g->c, res.const_val, VM_TYPE_BOOL);
            return res;
        }
        case AST_IDENT:     return gen_ident(g, n);
        case AST_FIELD_GET: return gen_field_get(g, n);
        case AST_CALL:      return gen_call(g, n);
        case AST_METHOD:    return gen_method(g, n);
        case AST_INDEX:     return gen_index(g, n);
        case AST_UNARY:     return gen_unary(g, n, expected);
        case AST_BINARY:    return gen_binary(g, n, expected);
        case AST_TRY:       return gen_try(g, n, expected);
        case AST_PROPAGATE: return gen_propagate(g, n);
        case AST_CATCH:     return gen_catch(g, n, expected);
        case AST_ARRAY:     return gen_array(g, n);
        case AST_MAP:       return gen_map(g, n);
        case AST_TUPLE:     return gen_tuple(g, n, expected);
        case AST_FUNC:      return gen_lambda(g, n);
        case AST_RANGE:
            gen_error(g, n, "Range '..' is only allowed in 'for' loops");
            return gen_result(VM_TYPE_I32);
        case AST_OPT_CHAIN: return gen_opt_chain(g, n);
        case AST_MATCH:     return gen_match_any(g, n, VM_TRUE, expected);
        case AST_NAMED_ARG:
            gen_error(g, n, "Named arguments are only allowed in a call: f { name = value }");
            return gen_result(VM_TYPE_I32);
        default:
            gen_error(g, n, "Expected expression");
            return gen_result(VM_TYPE_I32);
    }
}
