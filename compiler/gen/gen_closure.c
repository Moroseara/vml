#include "gen.h"

/*
 * Closures
 *
 * Before a function body is compiled, a pre-pass collects every identifier used
 * inside its nested functions. Locals of that function whose names appear there
 * are "boxed": their register holds a cell id and every access goes through the
 * cell, so the function and all closures share one variable.
 *
 * Inside a nested function, a name that is not a local resolves to an upvalue:
 * a boxed local of the enclosing function (VM_UPV_CELL), an unboxed one copied
 * when the closure is created (VM_UPV_SNAPSHOT, e.g. loop variables), or the
 * enclosing function's own upvalue (VM_UPV_PARENT) for deeper nesting.
 */

#define ND(g, n)     (&(g)->t->nodes[(n)])
#define LLEN(g, l)   ast_list_len((g)->t, (l))
#define LAT(g, l, i) ast_list_at((g)->t, (l), (i))

static GenFn *cur_fn(Gen *g) {
    return (g->fn_depth > 0) ? &g->fns[g->fn_depth - 1] : VM_NULL;
}

static void add_capture_name(GenFn *f, const Token *k) {
    for (int i = 0; i < f->num_cap; i++) {
        if (vm_str_eq(f->cap_name[i], f->cap_len[i], k->start, k->length) == VM_TRUE) return;
    }
    if (f->num_cap >= GEN_MAX_CAPTURES) {
        f->cap_overflow = VM_TRUE;   /* box everything rather than miss a capture */
        return;
    }
    f->cap_name[f->num_cap] = k->start;
    f->cap_len[f->num_cap] = k->length;
    f->num_cap++;
}

static void scan_list(Gen *g, GenFn *f, AstList l, vm_bool nested);

static void scan(Gen *g, GenFn *f, AstRef n, vm_bool nested) {
    if (n == 0) return;
    const AstNode *nd = ND(g, n);
    switch ((AstKind)nd->kind) {
        case AST_IDENT:
            if (nested) add_capture_name(f, &g->t->toks[nd->tok]);
            return;
        case AST_FUNC:
            scan(g, f, nd->b, VM_TRUE);
            return;
        case AST_BLOCK: case AST_RETURN: case AST_ARRAY: case AST_TUPLE: case AST_FSTRING:
            scan_list(g, f, nd->a, nested);
            return;
        case AST_LET:
            scan(g, f, nd->c, nested);
            return;
        case AST_IF:
            scan(g, f, nd->d, nested); scan(g, f, nd->a, nested); scan(g, f, nd->b, nested); scan(g, f, nd->c, nested);
            return;
        case AST_WHILE:
            scan(g, f, nd->d, nested); scan(g, f, nd->a, nested); scan(g, f, nd->b, nested);
            return;
        case AST_FOR:
            scan(g, f, nd->b, nested); scan(g, f, nd->c, nested);
            return;
        case AST_MATCH:
            scan(g, f, nd->a, nested); scan_list(g, f, nd->b, nested);
            return;
        case AST_ARM: case AST_BINARY: case AST_RANGE: case AST_INDEX: case AST_TRY: case AST_CATCH:
            scan(g, f, nd->a, nested); scan(g, f, nd->b, nested);
            return;
        case AST_DEFER: case AST_PRINT: case AST_YIELD: case AST_EXPR_STMT: case AST_UNARY: case AST_FIELD_GET:
        case AST_PROPAGATE: case AST_THROW:
            scan(g, f, nd->a, nested);
            return;
        case AST_ASSIGN: case AST_MAP:
            scan_list(g, f, nd->a, nested); scan_list(g, f, nd->b, nested);
            return;
        case AST_CALL: case AST_METHOD: case AST_OPT_CHAIN:
            scan(g, f, nd->a, nested); scan_list(g, f, nd->b, nested);
            return;
        default:
            return;   /* literals, types */
    }
}

static void scan_list(Gen *g, GenFn *f, AstList l, vm_bool nested) {
    for (vm_u32 i = 0; i < LLEN(g, l); i++) scan(g, f, LAT(g, l, i), nested);
}

void gen_fn_push(Gen *g, FuncState *enclosing, int fid, AstRef body) {
    if (g->fn_depth >= GEN_MAX_FN_DEPTH) {
        gen_error(g, body, "Functions nested too deeply");
        return;
    }
    GenFn *f = &g->fns[g->fn_depth++];
    f->enclosing = enclosing;
    f->fid = fid;
    f->num_upv = 0;
    f->num_cap = 0;
    f->cap_overflow = VM_FALSE;
    scan(g, f, body, VM_FALSE);
}

void gen_fn_pop(Gen *g) {
    if (g->fn_depth > 0) g->fn_depth--;
}

vm_bool gen_should_box(Gen *g, const char *name, vm_u32 len) {
    GenFn *f = cur_fn(g);
    if (f == VM_NULL) return VM_FALSE;
    if (f->cap_overflow) return VM_TRUE;
    for (int i = 0; i < f->num_cap; i++) {
        if (vm_str_eq(f->cap_name[i], f->cap_len[i], name, len) == VM_TRUE) return VM_TRUE;
    }
    return VM_FALSE;
}

/* Moves a scalar local into a fresh cell (in place: the register then holds the cell id) */
void gen_box_local(Gen *g, int loc) {
    Compiler *c = g->c;
    LocalVar *lv = &c->locals[loc];
    if (lv->boxed || lv->struct_id >= 0 || lv->elem_count > 1) return;
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CELL_NEW, lv->reg, lv->reg, (vm_u8)lv->type));
    lv->boxed = VM_TRUE;
}

/* Register holding the local's current value (loads through the cell when boxed) */
vm_u8 gen_local_value(Gen *g, int loc) {
    Compiler *c = g->c;
    LocalVar *lv = &c->locals[loc];
    if (!lv->boxed) return lv->reg;
    vm_u8 tmp = c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CELL_GET, tmp, lv->reg, 0));
    return tmp;
}

static int add_upval(Gen *g, GenFn *f, const char *name, vm_u32 len, vm_u8 kind, vm_u8 index, const LocalVar *origin) {
    if (f->num_upv >= VM_MAX_UPVALUES) {
        gen_error(g, 0, "Too many captured variables in one closure (max 16)");
        return -1;
    }
    GenUpval *u = &f->upv[f->num_upv];
    u->name = name;
    u->len = len;
    u->kind = kind;
    u->index = index;
    u->origin = *origin;
    return f->num_upv++;
}

/* -1: not found; -2: found but not capturable (struct / tuple value) */
static int resolve_at(Gen *g, int level, const char *name, vm_u32 len) {
    GenFn *f = &g->fns[level];
    for (int i = 0; i < f->num_upv; i++) {
        if (vm_str_eq(f->upv[i].name, f->upv[i].len, name, len) == VM_TRUE) return i;
    }
    if (level == 0 || f->enclosing == VM_NULL) return -1;
    FuncState *enc = f->enclosing;
    for (int i = enc->num_locals - 1; i >= 0; i--) {
        LocalVar *lv = &enc->locals[i];
        if (vm_str_eq(lv->name, lv->length, name, len) == VM_FALSE) continue;
        if (lv->struct_id >= 0 || lv->elem_count > 1) return -2;
        return add_upval(g, f, name, len, lv->boxed ? VM_UPV_CELL : VM_UPV_SNAPSHOT, lv->reg, lv);
    }
    int pi = resolve_at(g, level - 1, name, len);
    if (pi < 0) return pi;
    return add_upval(g, f, name, len, VM_UPV_PARENT, (vm_u8)pi, &g->fns[level - 1].upv[pi].origin);
}

int gen_resolve_upvalue(Gen *g, const char *name, vm_u32 len) {
    if (g->fn_depth == 0) return -1;
    return resolve_at(g, g->fn_depth - 1, name, len);
}

ExprResult gen_upvalue_get(Gen *g, int idx) {
    Compiler *c = g->c;
    const LocalVar *o = &cur_fn(g)->upv[idx].origin;
    vm_u8 tmp = c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_UPVAL_GET, tmp, (vm_u8)idx, 0));
    ExprResult res = gen_result(o->type);
    res.reg = tmp;
    res.elem_types[0] = o->elem_types[0];
    res.lambda_func_id = o->lambda_func_id;
    res.lambda_gen_id = o->lambda_gen_id;
    res.is_closure = o->is_closure;
    if (o->func_type_id >= 0) EXPR_SET_FUNC_TYPE(res, o->func_type_id);
    return res;
}

/* Copies the current function's capture list into its VMFunctionInfo (read by OP_CLOSURE) */
void gen_finish_closure_info(Gen *g, int fid) {
    GenFn *f = cur_fn(g);
    VMFunctionInfo *fn = &g->c->chunk->functions[fid];
    fn->num_upvals = (vm_u8)f->num_upv;
    for (int i = 0; i < f->num_upv; i++) {
        fn->upv_kind[i] = f->upv[i].kind;
        fn->upv_index[i] = f->upv[i].index;
        fn->upv_type[i] = (vm_u8)f->upv[i].origin.type;
    }
}
