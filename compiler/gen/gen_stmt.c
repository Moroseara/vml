#include "gen.h"

#define ND(g, n)     (&(g)->t->nodes[(n)])
#define LLEN(g, l)   ast_list_len((g)->t, (l))
#define LAT(g, l, i) ast_list_at((g)->t, (l), (i))

/* ---------------------------------------------------------------------------
 * Scopes & defer
 * ------------------------------------------------------------------------- */

void gen_begin_scope(Gen *g) {
    g->c->scope_depth++;
}

static void emit_defer(Gen *g, int idx) {
    AstTree *saved_t = g->t;
    int saved_num = g->num_defers;
    g->num_defers = idx;          /* a defer body never re-runs itself */
    g->t = g->defers[idx].t;
    AstRef node = g->defers[idx].node;
    if (g->t->nodes[node].kind == AST_BLOCK) gen_block(g, node);
    else gen_stmt(g, node);
    g->t = saved_t;
    g->num_defers = saved_num;
}

void gen_emit_defers_for_return(Gen *g) {
    for (int i = g->num_defers - 1; i >= 0; i--) emit_defer(g, i);
}

void gen_end_scope(Gen *g) {
    Compiler *c = g->c;
    while (g->num_defers > 0 && g->defers[g->num_defers - 1].depth == c->scope_depth) {
        int idx = --g->num_defers;
        emit_defer(g, idx);
    }
    while (c->num_locals > 0 && c->locals[c->num_locals - 1].depth == c->scope_depth) c->num_locals--;
    c->scope_depth--;
}

void gen_block(Gen *g, AstRef block) {
    if (block == 0) return;
    gen_begin_scope(g);
    AstList stmts = ND(g, block)->a;
    for (vm_u32 i = 0; i < LLEN(g, stmts) && !g->c->had_error; i++) gen_stmt(g, LAT(g, stmts, i));
    gen_end_scope(g);
}

void gen_block_scoped(Gen *g, AstRef block) {
    gen_block(g, block);
}

/* ---------------------------------------------------------------------------
 * Loop bookkeeping
 * ------------------------------------------------------------------------- */

static void patch_continues(Gen *g, int prev_continues, vm_u32 target) {
    Compiler *c = g->c;
    while (c->num_continues > prev_continues) {
        vm_u32 idx = c->continue_patches[--c->num_continues];
        gen_patch_jump_to(g, idx, target);
    }
}

static void patch_breaks(Gen *g, int prev_breaks) {
    Compiler *c = g->c;
    while (c->num_breaks > prev_breaks) {
        vm_u32 idx = c->break_patches[--c->num_breaks];
        gen_patch_jump(g, idx);
    }
}

/* ---------------------------------------------------------------------------
 * let / const
 * ------------------------------------------------------------------------- */

static vm_u8 declare_local_tok(Gen *g, vm_u32 tok, VMDataType type, vm_bool is_const, VMReg const_val) {
    return c_declare_local(g->c, gen_token(g, tok), type, is_const, const_val);
}

/* Boxes the most recently declared local if a nested function refers to its name */
static void box_if_captured(Gen *g) {
    Compiler *c = g->c;
    int loc = c->num_locals - 1;
    if (loc < 0) return;
    if (gen_should_box(g, c->locals[loc].name, c->locals[loc].length)) gen_box_local(g, loc);
}

static void allocate_struct_var(Gen *g, vm_u32 name_tok, int struct_id, vm_bool zero_init, const ExprResult *init) {
    Compiler *c = g->c;
    StructDef *sd = &c->structs[struct_id];
    vm_u8 base_reg = declare_local_tok(g, name_tok, sd->field_types[0], VM_FALSE, (VMReg){0});
    LocalVar *lv = &c->locals[c->num_locals - 1];
    lv->struct_id = struct_id;
    lv->elem_count = sd->num_fields;
    for (vm_u8 f = 0; f < sd->num_fields; f++) {
        lv->elem_types[f] = sd->field_types[f];
        if (f > 0) c_alloc_reg(c);
        if (zero_init == VM_TRUE) {
            VMReg init_v = (sd->field_has_default[f] == VM_TRUE) ? sd->field_defaults[f] : (VMReg){0};
            if (sd->field_has_default[f] == VM_FALSE && sd->field_types[f] == VM_TYPE_STRING) {
                init_v.str_id = vm_chunk_add_string(c->chunk, "", 0);
            }
            vm_u8 kr = c_emit_load_const(c, init_v, sd->field_types[f]);
            if (kr != (vm_u8)(base_reg + f)) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(base_reg + f), kr, 0));
            c->next_reg = (vm_u8)(base_reg + f + 1);
        } else if (init != VM_NULL && (vm_u8)(init->reg + f) != (vm_u8)(base_reg + f)) {
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(base_reg + f), (vm_u8)(init->reg + f), 0));
        }
    }
}

static void gen_let(Gen *g, AstRef n) {
    Compiler *c = g->c;
    vm_bool is_const = vm_bool_from_int((ND(g, n)->flags & AST_F_CONST) != 0);
    AstList names = ND(g, n)->a;
    AstRef ty = ND(g, n)->b;
    AstRef init_node = ND(g, n)->c;

    /* `let a, b = tuple_or_multi_return` */
    if (LLEN(g, names) > 1) {
        vm_u8 saved_reg = c->next_reg;
        ExprResult tup = gen_expr(g, init_node, GEN_ANY);
        c->next_reg = saved_reg;
        for (vm_u32 i = 0; i < LLEN(g, names); i++) {
            VMDataType et = (i < tup.elem_count) ? tup.elem_types[i] : tup.type;
            vm_u8 r = declare_local_tok(g, LAT(g, names, i), et, is_const, (VMReg){0});
            if (r != (vm_u8)(tup.reg + i)) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, r, (vm_u8)(tup.reg + i), 0));
        }
        /* Box after all values are placed: boxing rewrites the registers in place */
        for (vm_u32 i = 0; i < LLEN(g, names); i++) {
            int loc = c->num_locals - (int)LLEN(g, names) + (int)i;
            if (gen_should_box(g, c->locals[loc].name, c->locals[loc].length)) gen_box_local(g, loc);
        }
        return;
    }

    vm_u32 name_tok = LAT(g, names, 0);
    VMDataType type = GEN_ANY;
    vm_bool has_explicit_type = VM_FALSE;
    int struct_id = -1;
    int var_func_tid = -1;
    VMDataType decl_elem_type = VM_TYPE_VOID;

    if (ty != 0) {
        type = gen_type(g, ty, &struct_id, &decl_elem_type, &var_func_tid);
        has_explicit_type = VM_TRUE;
        /* `let b: Box<int>`: specialise a generic struct's field types */
        if (struct_id >= 0 && ND(g, ty)->kind == AST_TY_NAME && LLEN(g, ND(g, ty)->a) > 0 &&
            c->structs[struct_id].is_generic == VM_TRUE) {
            VMDataType gt = gen_type(g, LAT(g, ND(g, ty)->a, 0), VM_NULL, VM_NULL, VM_NULL);
            for (vm_u8 f = 0; f < c->structs[struct_id].num_fields; f++) c->structs[struct_id].field_types[f] = gt;
        }
        if (struct_id >= 0 && init_node == 0) {
            allocate_struct_var(g, name_tok, struct_id, VM_TRUE, VM_NULL);
            return;
        }
    }

    VMReg zero = {0};
    if (type == VM_TYPE_STRING) zero.str_id = vm_chunk_add_string(c->chunk, "", 0);

    if (init_node == 0) {
        if (type == GEN_ANY) type = VM_TYPE_I64;
        vm_u8 var_reg = declare_local_tok(g, name_tok, type, is_const, zero);
        if (var_func_tid >= 0) c->locals[c->num_locals - 1].func_type_id = var_func_tid;
        vm_u8 kreg = c_emit_load_const(c, zero, type);
        if (kreg != var_reg) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, var_reg, kreg, 0));
        c->next_reg = (vm_u8)(var_reg + 1);
        box_if_captured(g);
        return;
    }

    vm_u8 saved_reg = c->next_reg;
    c->last_empty_array_ip = 0xFFFFFFFFu;
    if (type == VM_TYPE_ARRAY && decl_elem_type != VM_TYPE_VOID) g->elem_hint = decl_elem_type;
    ExprResult init = gen_expr(g, init_node, type);
    g->elem_hint = GEN_ANY;
    if (has_explicit_type == VM_FALSE) {
        if (init.is_none) {
            gen_error(g, n, "'let x = none' needs a type: let x: T? = none");
            return;
        }
        type = init.type;
        struct_id = init.struct_id;
    } else if (gen_sid_is_optional(g, struct_id) || init.is_none || gen_is_optional(g, init) ||
               gen_sid_is_result(g, struct_id) || gen_is_result(g, init)) {
        init = gen_coerce(g, init, type, struct_id, n);
    } else if (struct_id >= 0 && init.struct_id >= 0) {
        (void)c_check_no_object_slicing(c, struct_id, init.struct_id);
    } else if (struct_id < 0) {
        init = gen_coerce(g, init, type, -1, n);
    }
    c->next_reg = saved_reg;

    if (struct_id >= 0) {
        allocate_struct_var(g, name_tok, struct_id, VM_FALSE, &init);
        return;
    }

    vm_bool var_const = vm_bool_from_int(is_const == VM_TRUE && init.is_const == VM_TRUE);
    vm_u8 var_reg = declare_local_tok(g, name_tok, type, var_const, init.const_val);
    LocalVar *lv = &c->locals[c->num_locals - 1];
    lv->lambda_func_id = init.lambda_func_id;
    lv->is_closure = init.is_closure;
    lv->lambda_gen_id = (init.lambda_gen_id >= 0) ? init.lambda_gen_id : -1;
    lv->func_type_id = (var_func_tid >= 0) ? var_func_tid : EXPR_GET_FUNC_TYPE(init);
    lv->elem_count = init.elem_count;
    for (vm_u8 k = 0; k < init.elem_count; k++) {
        lv->elem_types[k] = init.elem_types[k];
        if (k > 0) c_alloc_reg(c);
    }
    if (type == VM_TYPE_ARRAY && decl_elem_type != VM_TYPE_VOID) {
        if (lv->elem_count == 0) lv->elem_count = 1;
        lv->elem_types[0] = decl_elem_type;
        vm_u32 ip = c->last_empty_array_ip;
        if (ip != 0xFFFFFFFFu && VM_GET_OP(c->chunk->code[ip]) == OP_ARR_NEW && VM_GET_A(c->chunk->code[ip]) == init.reg) {
            c->chunk->code[ip] = VM_MAKE_ABC(OP_ARR_NEW, init.reg, (vm_u8)decl_elem_type, 0);
        }
    }
    if (init.lambda_gen_id < 0 && init.reg != var_reg) {
        for (vm_u8 k = 0; k < init.elem_count; k++) {
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(var_reg + k), (vm_u8)(init.reg + k), 0));
        }
    }
    box_if_captured(g);
}

/* ---------------------------------------------------------------------------
 * if / while / match
 * ------------------------------------------------------------------------- */

static vm_bool const_truthy(Gen *g, AstRef cond_node) {
    Compiler *c = g->c;
    vm_u32 saved_count = c->chunk->code_count;
    vm_u8 saved_reg = c->next_reg;
    ExprResult cond = gen_expr(g, cond_node, VM_TYPE_BOOL);
    c->chunk->code_count = saved_count;
    c->next_reg = saved_reg;
    return (cond.type == VM_TYPE_BOOL) ? cond.const_val.b : vm_bool_from_int(cond.const_val.i32 != 0);
}

/* `if let x = opt { }` / `while let x = opt { }`: binds the value when present */
static vm_u32 gen_let_unwrap(Gen *g, vm_u32 name_tok, ExprResult init) {
    Compiler *c = g->c;
    vm_u32 jmp = gen_emit_jump(g, OP_JMP_FALSE, init.reg);
    VMDataType inner = c->structs[init.struct_id].opt_inner;
    vm_u8 var_reg = declare_local_tok(g, name_tok, inner, VM_FALSE, (VMReg){0});
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, var_reg, (vm_u8)(init.reg + 1), 0));
    return jmp;
}

/* `if let x = e; cond` / `while let ...`: declares x, returns the exit jump */
static vm_u32 gen_let_head(Gen *g, AstRef node, vm_u32 *loop_start) {
    Compiler *c = g->c;
    AstRef let = ND(g, node)->d;
    vm_u32 name_tok = LAT(g, ND(g, let)->a, 0);
    AstRef ty = ND(g, let)->b;
    if (ty == 0 && ND(g, node)->a == 0) {
        /* No condition: if the value is optional, the condition is "it is present" */
        vm_u32 start = c->chunk->code_count;
        vm_u32 saved_count = start;
        vm_u8 saved_reg = c->next_reg;
        ExprResult probe = gen_expr(g, ND(g, let)->c, GEN_ANY);
        if (gen_is_optional(g, probe)) {
            if (loop_start != VM_NULL) *loop_start = start;
            return gen_let_unwrap(g, name_tok, probe);
        }
        c->chunk->code_count = saved_count;   /* not optional: compile normally below */
        c->next_reg = saved_reg;
    }
    VMDataType type = (ty != 0) ? gen_type(g, ty, VM_NULL, VM_NULL, VM_NULL) : GEN_ANY;
    vm_u8 var_reg;
    if (loop_start != VM_NULL) {
        /* while let: the binding is re-evaluated each iteration */
        if (type == GEN_ANY) type = VM_TYPE_I64;
        var_reg = declare_local_tok(g, name_tok, type, VM_FALSE, (VMReg){0});
        *loop_start = c->chunk->code_count;
        ExprResult init = gen_convert(g, gen_expr(g, ND(g, let)->c, type), type);
        if (init.reg != var_reg) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, var_reg, init.reg, 0));
    } else {
        vm_u8 saved_reg = c->next_reg;
        ExprResult init = gen_expr(g, ND(g, let)->c, type);
        if (ty == 0) type = init.type;
        else init = gen_convert(g, init, type);
        c->next_reg = saved_reg;
        var_reg = declare_local_tok(g, name_tok, type, VM_FALSE, init.const_val);
        if (init.reg != var_reg) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, var_reg, init.reg, 0));
    }
    if (ND(g, node)->a != 0) {
        ExprResult cond = gen_expr(g, ND(g, node)->a, VM_TYPE_BOOL);
        return gen_emit_jump(g, OP_JMP_FALSE, cond.reg);
    }
    return gen_emit_jump(g, (type == VM_TYPE_I64) ? OP_JMP_NZ_I64 : OP_JMP_NZ_I32, var_reg);
}

static void gen_else(Gen *g, AstRef els) {
    if (ND(g, els)->kind == AST_IF) gen_stmt(g, els);
    else gen_block(g, els);
}

/* True when everything emitted since `from` only loads constants (safe to drop) */
static vm_bool only_loads(Compiler *c, vm_u32 from) {
    for (vm_u32 i = from; i < c->chunk->code_count; i++) {
        VMOpCode op = (VMOpCode)VM_GET_OP(c->chunk->code[i]);
        if (op != OP_LOAD_BOOL && op != OP_LOADK && op != OP_LOAD_IMM_I32 && op != OP_LOAD_IMM_I64 && op != OP_MOVE) return VM_FALSE;
    }
    return VM_TRUE;
}

static void gen_if(Gen *g, AstRef n) {
    Compiler *c = g->c;
    AstRef then = ND(g, n)->b;
    AstRef els = ND(g, n)->c;

    if (ND(g, n)->flags & AST_F_CONST) {
        if (const_truthy(g, ND(g, n)->a) == VM_TRUE) gen_block(g, then);
        else if (els != 0) gen_else(g, els);
        return;
    }

    vm_u32 jmp_false;
    if (ND(g, n)->flags & AST_F_LET) {
        gen_begin_scope(g);
        jmp_false = gen_let_head(g, n, VM_NULL);
    } else {
        vm_u32 cond_start = c->chunk->code_count;
        vm_u8 cond_reg = c->next_reg;
        ExprResult cond = gen_expr(g, ND(g, n)->a, VM_TYPE_BOOL);
        if (cond.is_const && cond.type == VM_TYPE_BOOL && only_loads(c, cond_start)) {
            /* Known at compile time (e.g. `T is int` in a generic instance): keep only the taken branch */
            c->chunk->code_count = cond_start;
            c->next_reg = cond_reg;
            if (cond.const_val.b) gen_block(g, then);
            else if (els != 0) gen_else(g, els);
            return;
        }
        jmp_false = gen_emit_jump(g, OP_JMP_FALSE, cond.reg);
    }
    gen_block(g, then);
    if (els != 0) {
        vm_u32 jmp_end = gen_emit_jump(g, OP_JMP, 0);
        gen_patch_jump(g, jmp_false);
        gen_else(g, els);
        gen_patch_jump(g, jmp_end);
    } else {
        gen_patch_jump(g, jmp_false);
    }
    if (ND(g, n)->flags & AST_F_LET) gen_end_scope(g);
    (void)c;
}

static void gen_while(Gen *g, AstRef n) {
    Compiler *c = g->c;
    int prev_breaks = c->num_breaks;
    int prev_continues = c->num_continues;
    AstRef body = ND(g, n)->b;

    if (ND(g, n)->flags & AST_F_CONST) {
        if (const_truthy(g, ND(g, n)->a) == VM_FALSE) return;
        vm_u32 loop_start = c->chunk->code_count;
        gen_block(g, body);
        patch_continues(g, prev_continues, loop_start);
        gen_emit_jump_back(g, OP_JMP, 0, loop_start);
        patch_breaks(g, prev_breaks);
        return;
    }

    if (ND(g, n)->flags & AST_F_LET) {
        gen_begin_scope(g);
        vm_u32 loop_start = 0;
        vm_u32 exit_jmp = gen_let_head(g, n, &loop_start);
        gen_block(g, body);
        patch_continues(g, prev_continues, loop_start);
        gen_emit_jump_back(g, OP_JMP, 0, loop_start);
        gen_patch_jump(g, exit_jmp);
        patch_breaks(g, prev_breaks);
        gen_end_scope(g);
        return;
    }

    vm_u32 loop_start = c->chunk->code_count;
    ExprResult cond = gen_expr(g, ND(g, n)->a, VM_TYPE_BOOL);
    vm_u32 exit_jmp = gen_emit_jump(g, OP_JMP_FALSE, cond.reg);
    gen_block(g, body);
    patch_continues(g, prev_continues, loop_start);
    gen_emit_jump_back(g, OP_JMP, 0, loop_start);
    gen_patch_jump(g, exit_jmp);
    patch_breaks(g, prev_breaks);
}

/* ---------------------------------------------------------------------------
 * for
 * ------------------------------------------------------------------------- */

/* Shared tail of index-based loops: body, `i += 1`, back jump, exit patch */
static void finish_index_loop(Gen *g, AstRef body, vm_u8 reg_i, vm_u32 loop_start, vm_u32 exit_jmp,
                              int prev_breaks, int prev_continues) {
    Compiler *c = g->c;
    gen_block(g, body);
    patch_continues(g, prev_continues, c->chunk->code_count);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ADD_IMM_I32, reg_i, reg_i, 1));
    gen_emit_jump_back(g, OP_JMP, 0, loop_start);
    gen_patch_jump(g, exit_jmp);
    patch_breaks(g, prev_breaks);
}

static void rename_local(Gen *g, int slot, const char *name, vm_u32 len) {
    g->c->locals[slot].name = name;
    g->c->locals[slot].length = len;
}

static void gen_for(Gen *g, AstRef n) {
    Compiler *c = g->c;
    gen_begin_scope(g);
    int prev_breaks = c->num_breaks;
    int prev_continues = c->num_continues;
    AstList vars = ND(g, n)->a;
    AstRef iter = ND(g, n)->b;
    AstRef body = ND(g, n)->c;

    vm_bool two_vars = vm_bool_from_int(LLEN(g, vars) >= 2);
    vm_u32 var1 = LAT(g, vars, 0);
    vm_u32 var2 = two_vars ? LAT(g, vars, 1) : var1;
    const Token *v1 = gen_tok(g, var1);
    const Token *val_t = gen_tok(g, var2);

    /* Reserve reg_i and reg_limit first: OP_FORLOOP_I32 needs them contiguous */
    vm_u8 reg_i = declare_local_tok(g, var1, VM_TYPE_I32, VM_FALSE, (VMReg){0});
    int idx_slot = c->num_locals - 1;
    vm_u8 reg_limit = c_alloc_reg(c);

    vm_bool is_range = vm_bool_from_int(ND(g, iter)->kind == AST_RANGE);
    ExprResult first = gen_expr(g, is_range ? ND(g, iter)->a : iter, GEN_ANY);
    const char *idx_name = two_vars ? v1->start : "_iter_idx";
    vm_u32 idx_len = two_vars ? v1->length : 9;

    if (!is_range && first.struct_id < 0 && first.type == VM_TYPE_STRING) {
        rename_local(g, idx_slot, idx_name, idx_len);
        vm_u8 reg_ch = c_declare_local(c, *val_t, VM_TYPE_STRING, VM_FALSE, (VMReg){0});
        vm_u8 reg_str = c_alloc_reg(c);
        if (first.reg != reg_str) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_str, first.reg, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRLEN, reg_limit, reg_str, 0));
        vm_u32 loop_start = c->chunk->code_count;
        vm_u8 cmp_reg = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LT_I32, cmp_reg, reg_i, reg_limit));
        vm_u32 exit_jmp = gen_emit_jump(g, OP_JMP_FALSE, cmp_reg);
        vm_u8 abase = c_alloc_reg(c);
        vm_u8 cnt_reg = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, abase, reg_i, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, cnt_reg, 1));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRSUB, reg_ch, reg_str, abase));
        finish_index_loop(g, body, reg_i, loop_start, exit_jmp, prev_breaks, prev_continues);
        gen_end_scope(g);
        return;
    }

    if (!is_range && first.struct_id < 0 && first.type == VM_TYPE_ARRAY) {
        rename_local(g, idx_slot, idx_name, idx_len);
        VMDataType elem_t = first.elem_types[0];
        if (elem_t == VM_TYPE_ARRAY || elem_t == VM_TYPE_VOID) elem_t = VM_TYPE_I32;
        vm_u8 reg_val = c_declare_local(c, *val_t, elem_t, VM_FALSE, (VMReg){0});
        vm_u8 reg_arr = c_alloc_reg(c);
        if (first.reg != reg_arr) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_arr, first.reg, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_LEN, reg_limit, reg_arr, 0));
        vm_u32 loop_start = c->chunk->code_count;
        vm_u8 cmp_reg = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LT_I32, cmp_reg, reg_i, reg_limit));
        vm_u32 exit_jmp = gen_emit_jump(g, OP_JMP_FALSE, cmp_reg);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_GET, reg_val, reg_arr, reg_i));
        finish_index_loop(g, body, reg_i, loop_start, exit_jmp, prev_breaks, prev_continues);
        gen_end_scope(g);
        return;
    }

    if (!is_range && first.struct_id < 0 && first.type == VM_TYPE_MAP) {
        rename_local(g, idx_slot, "_map_idx", 8);
        VMDataType vt = first.elem_types[0];
        if (vt == VM_TYPE_MAP || vt == VM_TYPE_VOID) vt = VM_TYPE_I32;
        vm_u8 reg_k = c_declare_local(c, *v1, VM_TYPE_STRING, VM_FALSE, (VMReg){0});
        vm_u8 reg_v = 0;
        if (two_vars) reg_v = c_declare_local(c, *val_t, vt, VM_FALSE, (VMReg){0});
        vm_u8 reg_map = c_alloc_reg(c);
        if (first.reg != reg_map) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_map, first.reg, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_LEN, reg_limit, reg_map, 0));
        vm_u32 loop_start = c->chunk->code_count;
        vm_u8 cmp_reg = c_alloc_reg(c);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LT_I32, cmp_reg, reg_i, reg_limit));
        vm_u32 exit_jmp = gen_emit_jump(g, OP_JMP_FALSE, cmp_reg);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_KEY_AT, reg_k, reg_map, reg_i));
        if (two_vars) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_VAL_AT, reg_v, reg_map, reg_i));
        finish_index_loop(g, body, reg_i, loop_start, exit_jmp, prev_breaks, prev_continues);
        gen_end_scope(g);
        return;
    }

    if (!is_range && first.struct_id >= 0) {
        int sid = first.struct_id;
        vm_u8 nf = c->structs[sid].num_fields;
        rename_local(g, idx_slot, idx_name, idx_len);
        vm_u8 s_base = c_alloc_reg(c);
        for (vm_u8 f = 1; f < nf; f++) c_alloc_reg(c);
        for (vm_u8 f = 0; f < nf; f++) {
            if ((vm_u8)(first.reg + f) != (vm_u8)(s_base + f)) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(s_base + f), (vm_u8)(first.reg + f), 0));
            }
        }
        ExprResult self_v = first;
        self_v.reg = s_base;

        int iter_fid = c_resolve_struct_method(c, sid, "op_iter", 7);
        if (iter_fid >= 0) {
            VMFunctionInfo *fn = &c->chunk->functions[iter_fid];
            VMDataType elem_t = (fn->return_reg_count >= 2) ? fn->return_elem_types[1] : fn->return_type;
            vm_u8 reg_val = c_declare_local(c, *val_t, elem_t, VM_FALSE, (VMReg){0});
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));
            vm_u32 loop_start = c->chunk->code_count;
            ExprResult idx_v = gen_result(VM_TYPE_I32);
            idx_v.reg = reg_i;
            ExprResult args[2] = {self_v, idx_v};
            ExprResult r = gen_call_fid(g, iter_fid, args, 2);
            vm_u32 exit_jmp = gen_emit_jump(g, OP_JMP_FALSE, r.reg);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_val, (vm_u8)(r.reg + 1), 0));
            finish_index_loop(g, body, reg_i, loop_start, exit_jmp, prev_breaks, prev_continues);
            gen_end_scope(g);
            return;
        }

        int len_fid = c_resolve_struct_method(c, sid, "op_len", 6);
        VMDataType idx_types[2] = {first.type, VM_TYPE_I32};
        int idx_sids[2] = {sid, -1};
        int idx_fid = c_resolve_struct_method_overload(c, sid, "op[]", 4, idx_types, idx_sids, 2);
        if (len_fid >= 0 && idx_fid >= 0) {
            VMFunctionInfo *idx_fn = &c->chunk->functions[idx_fid];
            vm_u8 reg_val = c_declare_local(c, *val_t, idx_fn->return_type, VM_FALSE, (VMReg){0});
            ExprResult lr = gen_call_fid(g, len_fid, &self_v, 1);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_limit, lr.reg, 0));
            vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, reg_i, 0));
            vm_u32 loop_start = c->chunk->code_count;
            vm_u8 cmp_reg = c_alloc_reg(c);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LT_I32, cmp_reg, reg_i, reg_limit));
            vm_u32 exit_jmp = gen_emit_jump(g, OP_JMP_FALSE, cmp_reg);
            ExprResult idx_v = gen_result(VM_TYPE_I32);
            idx_v.reg = reg_i;
            ExprResult args[2] = {self_v, idx_v};
            ExprResult ir = gen_call_fid(g, idx_fid, args, 2);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_val, ir.reg, 0));
            finish_index_loop(g, body, reg_i, loop_start, exit_jmp, prev_breaks, prev_continues);
            gen_end_scope(g);
            return;
        }
        gen_error(g, n, "Struct in 'for' loop must define 'operator iter' or both 'operator #' and 'operator []'");
        gen_end_scope(g);
        return;
    }

    /* Integer range: `for i in a..b` or `for i in n`; the loop variable is i64 unless both bounds are i32 */
    ExprResult end_e = first;
    if (is_range) end_e = gen_expr(g, ND(g, iter)->b, first.type);
    vm_bool wide = vm_bool_from_int(first.type != VM_TYPE_I32 || (is_range && end_e.type != VM_TYPE_I32));
    VMDataType it = wide ? VM_TYPE_I64 : VM_TYPE_I32;
    c->locals[idx_slot].type = it;
    c->locals[idx_slot].elem_types[0] = it;
    first = gen_convert(g, first, it);
    if (is_range) {
        end_e = gen_convert(g, end_e, it);
        if (first.reg != reg_i) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_i, first.reg, 0));
        if (end_e.reg != reg_limit) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_limit, end_e.reg, 0));
    } else {
        if (first.reg != reg_limit) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, reg_limit, first.reg, 0));
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(wide ? OP_LOAD_IMM_I64 : OP_LOAD_IMM_I32, reg_i, 0));
    }
    vm_u8 cmp_reg = c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(wide ? OP_LT_I64 : OP_LT_I32, cmp_reg, reg_i, reg_limit));
    vm_u32 skip_jmp = gen_emit_jump(g, OP_JMP_FALSE, cmp_reg);
    vm_u32 body_start = c->chunk->code_count;
    gen_block(g, body);
    patch_continues(g, prev_continues, c->chunk->code_count);
    gen_emit_jump_back(g, wide ? OP_FORLOOP_I64 : OP_FORLOOP_I32, reg_i, body_start);
    gen_patch_jump(g, skip_jmp);
    patch_breaks(g, prev_breaks);
    gen_end_scope(g);
}

/* ---------------------------------------------------------------------------
 * return / assignment
 * ------------------------------------------------------------------------- */

static void gen_return(Gen *g, AstRef n) {
    Compiler *c = g->c;
    AstList vals = ND(g, n)->a;
    vm_u32 count = LLEN(g, vals);
    if (count == 0 && gen_sid_is_result(g, c->current_ret_struct_id)) {
        ExprResult okv = gen_ok_void(g, c->current_ret_struct_id);
        gen_emit_defers_for_return(g);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, okv.reg, c->structs[okv.struct_id].num_fields, 0));
        return;
    }
    if (count == 0) {
        gen_emit_defers_for_return(g);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        return;
    }
    ExprResult val;
    if (count == 1) {
        val = gen_expr(g, LAT(g, vals, 0), c->current_ret_type);
        if (c->current_ret_struct_id < 0 || gen_sid_is_optional(g, c->current_ret_struct_id) ||
            gen_sid_is_result(g, c->current_ret_struct_id)) {
            val = gen_coerce(g, val, c->current_ret_type, c->current_ret_struct_id, n);
        }
    } else {
        /* `return a, b`: consecutive registers */
        ExprResult first = gen_expr(g, LAT(g, vals, 0), c->current_ret_type);
        vm_u8 base = c_alloc_reg(c);
        if (first.reg != base) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, base, first.reg, 0));
        val = first;
        val.reg = base;
        val.elem_count = 1;
        for (vm_u32 i = 1; i < count && i < MAX_TUPLE_ELEMS; i++) {
            vm_u8 slot = c_alloc_reg(c);
            vm_u8 save = c->next_reg;
            ExprResult e = gen_expr(g, LAT(g, vals, i), GEN_ANY);
            c->next_reg = save;
            if (e.reg != slot) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, slot, e.reg, 0));
            val.elem_types[val.elem_count++] = e.type;
        }
    }
    if (c->current_ret_struct_id >= 0 && val.struct_id >= 0) {
        (void)c_check_no_object_slicing(c, c->current_ret_struct_id, val.struct_id);
    }
    vm_u8 ret_count = (val.struct_id >= 0) ? c->structs[val.struct_id].num_fields : val.elem_count;
    if (ret_count == 0) ret_count = 1;
    if (c->next_reg < (vm_u8)(val.reg + ret_count)) {
        c->next_reg = (vm_u8)(val.reg + ret_count);
        if (c->next_reg > c->max_reg) c->max_reg = c->next_reg;
    }
    gen_emit_defers_for_return(g);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, val.reg, ret_count, 0));
}

/* Applies `target op= rhs` to a single register of known type */
static void apply_compound(Gen *g, TokenType op, vm_u8 target_reg, VMDataType ttype, ExprResult rhs) {
    Compiler *c = g->c;
    if (!(op == TOK_PLUS_EQ && ttype == VM_TYPE_STRING)) rhs = gen_convert(g, rhs, ttype);
    if (op == TOK_ASSIGN) {
        if (rhs.reg != target_reg) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, target_reg, rhs.reg, 0));
        return;
    }
    if (op == TOK_PLUS_EQ && ttype == VM_TYPE_STRING) {
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_STRCAT, target_reg, target_reg, rhs.reg));
        return;
    }
    VMOpCode vop;
    switch (op) {
        case TOK_PLUS_EQ:  vop = (ttype == VM_TYPE_F32) ? OP_ADD_F32 : (ttype == VM_TYPE_F64) ? OP_ADD_F64 : (ttype == VM_TYPE_I64) ? OP_ADD_I64 : OP_ADD_I32; break;
        case TOK_MINUS_EQ: vop = (ttype == VM_TYPE_F32) ? OP_SUB_F32 : (ttype == VM_TYPE_F64) ? OP_SUB_F64 : (ttype == VM_TYPE_I64) ? OP_SUB_I64 : OP_SUB_I32; break;
        case TOK_STAR_EQ:  vop = (ttype == VM_TYPE_F32) ? OP_MUL_F32 : (ttype == VM_TYPE_F64) ? OP_MUL_F64 : (ttype == VM_TYPE_I64) ? OP_MUL_I64 : OP_MUL_I32; break;
        case TOK_SLASH_EQ: vop = (ttype == VM_TYPE_F32) ? OP_DIV_F32 : (ttype == VM_TYPE_F64) ? OP_DIV_F64 : (ttype == VM_TYPE_I64) ? OP_DIV_I64 : OP_DIV_I32; break;
        case TOK_MOD_EQ:   vop = (ttype == VM_TYPE_I64) ? OP_MOD_I64 : OP_MOD_I32; break;
        case TOK_BAND_EQ:  vop = (ttype == VM_TYPE_I64) ? OP_BAND_I64 : OP_BAND_I32; break;
        case TOK_BOR_EQ:   vop = (ttype == VM_TYPE_I64) ? OP_BOR_I64 : OP_BOR_I32; break;
        case TOK_BXOR_EQ:  vop = (ttype == VM_TYPE_I64) ? OP_BXOR_I64 : OP_BXOR_I32; break;
        case TOK_SHL_EQ:   vop = (ttype == VM_TYPE_I64) ? OP_SHL_I64 : OP_SHL_I32; break;
        default:           vop = (ttype == VM_TYPE_I64) ? OP_SHR_I64 : OP_SHR_I32; break;
    }
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(vop, target_reg, target_reg, rhs.reg));
}

static const char *compound_op_name(TokenType op) {
    switch (op) {
        case TOK_PLUS_EQ:  return "op+";
        case TOK_MINUS_EQ: return "op-";
        case TOK_STAR_EQ:  return "op*";
        case TOK_SLASH_EQ: return "op/";
        case TOK_MOD_EQ:   return "op%";
        case TOK_BAND_EQ:  return "op&";
        case TOK_BOR_EQ:   return "op|";
        case TOK_BXOR_EQ:  return "op^";
        case TOK_SHL_EQ:   return "op<<";
        default:           return "op>>";
    }
}

static void assign_local(Gen *g, AstRef stmt, TokenType op, int loc, AstRef rhs_node) {
    Compiler *c = g->c;
    if (c->locals[loc].is_const == VM_TRUE) {
        gen_error(g, stmt, "Invalid assignment target");
        return;
    }
    vm_u8 target_reg = c->locals[loc].reg;
    VMDataType ttype = c->locals[loc].type;
    int sid = c->locals[loc].struct_id;

    if (c->locals[loc].boxed) {
        /* Captured variable: read-modify-write through its cell */
        ExprResult rhs;
        vm_u8 val_reg;
        if (op == TOK_ASSIGN) {
            rhs = gen_convert(g, gen_expr(g, rhs_node, ttype), ttype);
            if (ttype == VM_TYPE_ARRAY || ttype == VM_TYPE_MAP) c->locals[loc].elem_types[0] = rhs.elem_types[0];
            c->locals[loc].is_closure = rhs.is_closure;
            val_reg = rhs.reg;
        } else {
            val_reg = gen_local_value(g, loc);
            rhs = gen_expr(g, rhs_node, ttype);
            apply_compound(g, op, val_reg, ttype, rhs);
        }
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CELL_SET, target_reg, val_reg, 0));
        return;
    }

    if (op == TOK_ASSIGN) {
        ExprResult rhs = gen_expr(g, rhs_node, ttype);
        if (ttype == VM_TYPE_ARRAY || ttype == VM_TYPE_MAP) c->locals[loc].elem_types[0] = rhs.elem_types[0];
        rhs = gen_coerce(g, rhs, ttype, sid, stmt);
        if (sid >= 0) {
            if (c_check_no_object_slicing(c, sid, rhs.struct_id) == VM_TRUE) {
                vm_u8 nf = c->structs[sid].num_fields;
                for (vm_u8 f = 0; f < nf; f++) {
                    if ((vm_u8)(rhs.reg + f) != (vm_u8)(target_reg + f)) {
                        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(target_reg + f), (vm_u8)(rhs.reg + f), 0));
                    }
                }
            }
        } else if (rhs.reg != target_reg) {
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, target_reg, rhs.reg, 0));
        }
        return;
    }

    vm_u32 before_rhs = c->chunk->code_count;
    ExprResult rhs = gen_expr(g, rhs_node, ttype);
    if (sid >= 0) {
        const char *op_name = compound_op_name(op);
        VMDataType arg_types[2] = {ttype, rhs.type};
        int arg_sids[2] = {sid, rhs.struct_id};
        int fid = c_resolve_struct_method_overload(c, sid, op_name, vm_str_len(op_name), arg_types, arg_sids, 2);
        if (fid < 0) {
            gen_error(g, stmt, "Struct does not define matching operator for compound assignment");
            return;
        }
        ExprResult self_v = gen_result(ttype);
        self_v.reg = target_reg;
        self_v.struct_id = sid;
        ExprResult args[2] = {self_v, rhs};
        ExprResult r = gen_call_fid(g, fid, args, 2);
        vm_u8 nf = c->structs[sid].num_fields;
        for (vm_u8 f = 0; f < nf; f++) {
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(target_reg + f), (vm_u8)(r.reg + f), 0));
        }
        return;
    }
    vm_i64 rimm = (rhs.type == VM_TYPE_I64) ? rhs.const_val.i64 : (vm_i64)rhs.const_val.i32;
    if ((op == TOK_PLUS_EQ || op == TOK_MINUS_EQ) && (ttype == VM_TYPE_I32 || ttype == VM_TYPE_I64) &&
        rhs.type == ttype && rhs.is_const == VM_TRUE && rimm >= -127 && rimm <= 127 &&
        c->chunk->code_count == before_rhs + 1) {
        c->chunk->code_count = before_rhs;
        vm_i8 imm = (op == TOK_PLUS_EQ) ? (vm_i8)rimm : (vm_i8)(-rimm);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC((ttype == VM_TYPE_I64) ? OP_ADD_IMM_I64 : OP_ADD_IMM_I32, target_reg, target_reg, (vm_u8)imm));
        return;
    }
    apply_compound(g, op, target_reg, ttype, rhs);
}

static void assign_field(Gen *g, AstRef stmt, TokenType op, AstRef target, AstRef rhs_node) {
    Compiler *c = g->c;
    AstRef obj = ND(g, target)->a;
    if (ND(g, obj)->kind != AST_IDENT) {
        gen_error(g, stmt, "Invalid assignment target");
        return;
    }
    const Token *on = gen_tok(g, ND(g, obj)->tok);
    int loc = c_resolve_local(c, on->start, on->length);
    if (loc < 0 || c->locals[loc].struct_id < 0) {
        gen_error(g, stmt, "Invalid assignment target");
        return;
    }
    int sid = c->locals[loc].struct_id;
    StructDef *sd = &c->structs[sid];
    const Token *fname = gen_tok(g, ND(g, target)->tok);
    for (vm_u8 f = 0; f < sd->num_fields; f++) {
        if (vm_str_eq_cstr(fname->start, fname->length, sd->field_names[f]) == VM_TRUE) {
            if (c_can_access_struct_member(c, sid, sd->field_is_pub[f]) == VM_FALSE) {
                gen_error(g, stmt, "Cannot modify private struct field (missing 'pub') from outside its struct");
            }
            ExprResult rhs = gen_expr(g, rhs_node, sd->field_types[f]);
            apply_compound(g, op, (vm_u8)(c->locals[loc].reg + f), sd->field_types[f], rhs);
            return;
        }
    }
    gen_error(g, stmt, "Unknown member or field");
}

static void assign_index(Gen *g, AstRef stmt, TokenType op, AstRef target, AstRef rhs_node) {
    Compiler *c = g->c;
    AstRef obj = ND(g, target)->a;
    AstRef idx_node = ND(g, target)->b;
    if (ND(g, obj)->kind != AST_IDENT) {
        gen_error(g, stmt, "Invalid assignment target");
        return;
    }
    const Token *on = gen_tok(g, ND(g, obj)->tok);
    int loc = c_resolve_local(c, on->start, on->length);
    if (op != TOK_ASSIGN) {
        gen_error(g, stmt, "Compound assignment to an index is not supported");
        return;
    }
    if (loc < 0 || c->locals[loc].boxed) {
        /* Captured array / map: get the reference, then store into it */
        ExprResult o = gen_expr(g, obj, VM_TYPE_I32);
        if (o.struct_id < 0 && o.type == VM_TYPE_ARRAY) {
            ExprResult idx = gen_expr(g, idx_node, VM_TYPE_I32);
            VMDataType et = (o.elem_types[0] == VM_TYPE_ARRAY || o.elem_types[0] == VM_TYPE_VOID) ? VM_TYPE_I32 : o.elem_types[0];
            ExprResult val = gen_expr(g, rhs_node, et);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_SET, o.reg, idx.reg, val.reg));
            return;
        }
        if (o.struct_id < 0 && o.type == VM_TYPE_MAP) {
            ExprResult key = gen_expr(g, idx_node, VM_TYPE_STRING);
            VMDataType vt = (o.elem_types[0] == VM_TYPE_MAP || o.elem_types[0] == VM_TYPE_VOID) ? VM_TYPE_I32 : o.elem_types[0];
            ExprResult val = gen_expr(g, rhs_node, vt);
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_SET, o.reg, key.reg, val.reg));
            return;
        }
        gen_error(g, stmt, "Invalid assignment target");
        return;
    }
    LocalVar *lv = &c->locals[loc];
    if (lv->struct_id < 0 && lv->type == VM_TYPE_ARRAY) {
        ExprResult idx = gen_expr(g, idx_node, VM_TYPE_I32);
        VMDataType et = lv->elem_types[0];
        if (et == VM_TYPE_ARRAY || et == VM_TYPE_VOID) et = VM_TYPE_I32;
        ExprResult val = gen_expr(g, rhs_node, et);
        if (lv->elem_types[0] != VM_TYPE_VOID) val = gen_convert(g, val, et);
        c->locals[loc].elem_types[0] = val.type;
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_ARR_SET, c->locals[loc].reg, idx.reg, val.reg));
        return;
    }
    if (lv->struct_id < 0 && lv->type == VM_TYPE_MAP) {
        ExprResult key = gen_expr(g, idx_node, VM_TYPE_STRING);
        VMDataType vt = lv->elem_types[0];
        if (vt == VM_TYPE_MAP || vt == VM_TYPE_VOID) vt = VM_TYPE_I32;
        ExprResult val = gen_expr(g, rhs_node, vt);
        c->locals[loc].elem_types[0] = val.type;
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MAP_SET, c->locals[loc].reg, key.reg, val.reg));
        return;
    }
    if (lv->struct_id >= 0) {
        int sid = lv->struct_id;
        ExprResult key = gen_expr(g, idx_node, GEN_ANY);
        ExprResult val = gen_expr(g, rhs_node, GEN_ANY);
        VMDataType arg_types[3] = {c->locals[loc].type, key.type, val.type};
        int arg_sids[3] = {sid, key.struct_id, val.struct_id};
        int fid = c_resolve_struct_method_overload(c, sid, "op[]=", 5, arg_types, arg_sids, 3);
        if (fid < 0) {
            gen_error(g, stmt, "Struct does not define matching 'operator []=' for index assignment");
            return;
        }
        ExprResult self_v = gen_result(c->locals[loc].type);
        self_v.reg = c->locals[loc].reg;
        self_v.struct_id = sid;
        ExprResult args[3] = {self_v, key, val};
        ExprResult r = gen_call_fid(g, fid, args, 3);
        if (c->chunk->functions[fid].return_struct_id == sid) {
            vm_u8 nf = c->structs[sid].num_fields;
            for (vm_u8 f = 0; f < nf; f++) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(c->locals[loc].reg + f), (vm_u8)(r.reg + f), 0));
            }
        }
        return;
    }
    gen_error(g, stmt, "'[]' index assignment requires an array, map, or struct with 'operator []='");
}

static void assign_one(Gen *g, AstRef stmt, TokenType op, AstRef target, AstRef rhs) {
    Compiler *c = g->c;
    switch (g->t->nodes[target].kind) {
        case AST_IDENT: {
            const Token *k = gen_tok(g, ND(g, target)->tok);
            int loc = c_resolve_local(c, k->start, k->length);
            if (loc >= 0) {
                assign_local(g, stmt, op, loc, rhs);
                return;
            }
            int up = gen_resolve_upvalue(g, k->start, k->length);
            if (up < 0) {
                gen_error(g, stmt, "Invalid assignment target");
                return;
            }
            const LocalVar *o = &g->fns[g->fn_depth - 1].upv[up].origin;
            if (o->is_const) {
                gen_error(g, stmt, "Cannot assign to a captured constant");
                return;
            }
            vm_u8 val_reg;
            if (op == TOK_ASSIGN) {
                val_reg = gen_convert(g, gen_expr(g, rhs, o->type), o->type).reg;
            } else {
                val_reg = gen_upvalue_get(g, up).reg;
                ExprResult r = gen_expr(g, rhs, o->type);
                apply_compound(g, op, val_reg, o->type, r);
            }
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_UPVAL_SET, (vm_u8)up, val_reg, 0));
            return;
        }
        case AST_FIELD_GET: assign_field(g, stmt, op, target, rhs); return;
        case AST_INDEX:     assign_index(g, stmt, op, target, rhs); return;
        default:
            gen_error(g, stmt, "Invalid assignment target");
            return;
    }
}

static void gen_assign(Gen *g, AstRef n) {
    Compiler *c = g->c;
    TokenType op = (TokenType)ND(g, n)->op;
    AstList targets = ND(g, n)->a;
    AstList vals = ND(g, n)->b;
    vm_u32 nt = LLEN(g, targets);
    vm_u32 nv = LLEN(g, vals);

    if (nt == 1 && nv == 1) {
        assign_one(g, n, op, LAT(g, targets, 0), LAT(g, vals, 0));
        return;
    }
    if (op != TOK_ASSIGN || nt != nv) {
        gen_error(g, n, "Multiple assignment needs '=' and the same number of targets and values");
        return;
    }
    /* `a, b = x, y`: evaluate every value first, then store (so `a, b = b, a` swaps) */
    vm_u8 temps[MAX_TUPLE_ELEMS];
    VMDataType types[MAX_TUPLE_ELEMS];
    for (vm_u32 i = 0; i < nv && i < MAX_TUPLE_ELEMS; i++) {
        ExprResult v = gen_expr(g, LAT(g, vals, i), GEN_ANY);
        temps[i] = c_alloc_reg(c);
        types[i] = v.type;
        if (v.reg != temps[i]) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, temps[i], v.reg, 0));
    }
    for (vm_u32 i = 0; i < nt && i < MAX_TUPLE_ELEMS; i++) {
        AstRef target = LAT(g, targets, i);
        if (ND(g, target)->kind != AST_IDENT) {
            gen_error(g, n, "Multiple assignment supports plain variables only");
            return;
        }
        const Token *k = gen_tok(g, ND(g, target)->tok);
        int loc = c_resolve_local(c, k->start, k->length);
        if (loc < 0 || c->locals[loc].is_const == VM_TRUE) {
            gen_error(g, n, "Invalid assignment target");
            return;
        }
        (void)types;
        if (c->locals[loc].boxed) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_CELL_SET, c->locals[loc].reg, temps[i], 0));
        else vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, c->locals[loc].reg, temps[i], 0));
    }
}

/* ---------------------------------------------------------------------------
 * Statement dispatcher
 * ------------------------------------------------------------------------- */

void gen_stmt(Gen *g, AstRef n) {
    Compiler *c = g->c;
    if (n == 0 || c->had_error) return;
    gen_at(g, n);
    vm_u8 saved_reg = c->next_reg;

    switch ((AstKind)ND(g, n)->kind) {
        case AST_FUNC: {
            /* Nested named function: visible to the rest of the block and to itself */
            c_declare_local(c, gen_token(g, ND(g, n)->tok), VM_TYPE_I32, VM_TRUE, (VMReg){0});
            int slot = c->num_locals - 1;
            c->locals[slot].lambda_func_id = (int)c->chunk->num_functions;
            ExprResult inner = gen_lambda(g, n);
            c->locals[slot].lambda_func_id = inner.lambda_func_id;
            c->locals[slot].lambda_gen_id = inner.lambda_gen_id;
            c->locals[slot].is_closure = inner.is_closure;
            if (inner.lambda_gen_id < 0 && inner.reg != c->locals[slot].reg) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, c->locals[slot].reg, inner.reg, 0));
            }
            c->next_reg = (vm_u8)(c->locals[slot].reg + 1);
            if (gen_should_box(g, c->locals[slot].name, c->locals[slot].length)) gen_box_local(g, slot);
            return;
        }
        case AST_LET:       gen_let(g, n); return;
        case AST_IF:        gen_if(g, n); break;
        case AST_MATCH:     (void)gen_match_any(g, n, VM_FALSE, VM_TYPE_VOID); break;
        case AST_WHILE:     gen_while(g, n); break;
        case AST_FOR:       gen_for(g, n); break;
        case AST_BREAK:
            if (c->num_breaks < 64) c->break_patches[c->num_breaks++] = gen_emit_jump(g, OP_JMP, 0);
            return;
        case AST_CONTINUE:
            if (c->num_continues < 64) c->continue_patches[c->num_continues++] = gen_emit_jump(g, OP_JMP, 0);
            return;
        case AST_PRINT:     gen_print(g, ND(g, n)->a); break;
        case AST_RETURN:    gen_return(g, n); break;
        case AST_DEFER:
            if (g->num_defers < GEN_MAX_DEFERS) {
                GenDefer *d = &g->defers[g->num_defers++];
                d->t = g->t;
                d->node = ND(g, n)->a;
                d->depth = c->scope_depth;
            }
            return;
        case AST_YIELD: {
            if (g->yield_active == VM_FALSE) {
                gen_error(g, n, "'=>' value is only allowed inside a 'try ... or' block");
                return;
            }
            ExprResult v = gen_expr(g, ND(g, n)->a, g->yield_type);
            if (g->yield_type != VM_TYPE_VOID) v = gen_convert(g, v, g->yield_type);
            if (v.reg != g->yield_dst) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, g->yield_dst, v.reg, 0));
            if (g->yield_type == VM_TYPE_VOID) g->yield_type = v.type;
            break;
        }
        case AST_BLOCK:     gen_block(g, n); break;
        case AST_ASSIGN:    gen_assign(g, n); break;
        case AST_THROW:     gen_throw(g, n); break;
        case AST_EXPR_STMT: {
            ExprResult r = gen_expr(g, ND(g, n)->a, VM_TYPE_VOID);
            if (gen_is_result(g, r)) gen_error(g, n, "Error result is ignored: handle it with '?' or 'catch'");
            break;
        }
        default:
            gen_error(g, n, "Unexpected statement");
            return;
    }
    c->next_reg = saved_reg;
}
