#include "gen.h"
#include "../module/module.h"
#include "../decl/decl.h"
#include "../../vm/stdlib/vm_stdlib.h"
#include "../../vm/stdlib/vm_intrinsics.h"
#include "../../vm/opt/vm_fuse.h"

#define ND(g, n)     (&(g)->t->nodes[(n)])
#define LLEN(g, l)   ast_list_len((g)->t, (l))
#define LAT(g, l, i) ast_list_at((g)->t, (l), (i))

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static FuncState *save_state(Gen *g, FuncState *s) {
    Compiler *c = g->c;
    s->num_locals = c->num_locals;
    for (int i = 0; i < c->num_locals; i++) s->locals[i] = c->locals[i];
    s->num_outer = c->num_outer_locals;
    for (int i = 0; i < c->num_outer_locals; i++) s->outer[i] = c->outer_locals[i];
    s->scope_depth = c->scope_depth;
    s->next_reg = c->next_reg;
    s->max_reg = c->max_reg;
    s->ret_type = c->current_ret_type;
    s->ret_sid = c->current_ret_struct_id;
    s->func_id = c->current_func_id;
    s->num_defers = g->num_defers;
    s->num_breaks = c->num_breaks;
    s->num_continues = c->num_continues;
    return s;
}

static void restore_state(Gen *g, const FuncState *s) {
    Compiler *c = g->c;
    c->num_locals = s->num_locals;
    for (int i = 0; i < s->num_locals; i++) c->locals[i] = s->locals[i];
    c->num_outer_locals = s->num_outer;
    for (int i = 0; i < s->num_outer; i++) c->outer_locals[i] = s->outer[i];
    c->scope_depth = s->scope_depth;
    c->next_reg = s->next_reg;
    c->max_reg = s->max_reg;
    c->current_ret_type = s->ret_type;
    c->current_ret_struct_id = s->ret_sid;
    c->current_func_id = s->func_id;
    g->num_defers = s->num_defers;
    c->num_breaks = s->num_breaks;
    c->num_continues = s->num_continues;
}

/* Evaluates a constant expression (default values) without leaving code behind */
static VMReg const_value(Gen *g, AstRef expr, VMDataType type, VMDataType *out_type) {
    Compiler *c = g->c;
    vm_u32 saved_cnt = c->chunk->code_count;
    vm_u8 saved_r = c->next_reg;
    ExprResult v = gen_expr(g, expr, type);
    c->chunk->code_count = saved_cnt;
    c->next_reg = saved_r;
    if (out_type != VM_NULL) *out_type = v.type;
    return v.const_val;
}

static void init_function(VMFunctionInfo *fn) {
    fn->arity = 0;
    fn->min_arity = 0;
    fn->is_pub = VM_TRUE;
    fn->is_deprecated = VM_FALSE;
    fn->coerce_args_to_string = VM_FALSE;
    fn->deprecated_msg[0] = '\0';
    fn->owner_struct_id = -1;
    fn->return_type = VM_TYPE_VOID;
    fn->return_struct_id = -1;
    fn->return_func_type_id = -1;
    fn->return_reg_count = 1;
    fn->env_map_id = -1;
    fn->enclosing_func_id = -1;
    fn->num_upvals = 0;
    for (int p = 0; p < VM_MAX_PARAMS; p++) fn->param_func_type[p] = -1;
}

const char *gen_operator_name(Gen *g, AstRef fn_node) {
    vm_u32 tok = ND(g, fn_node)->tok;
    vm_u8 count = ND(g, fn_node)->op;
    const Token *k = gen_tok(g, tok);
    if (count == 2 && k->type == TOK_LPAREN) return "op_call";
    if (count == 2 && k->type == TOK_LBRACKET) return "op[]";
    if (count == 3) return "op[]=";
    switch (k->type) {
        case TOK_PLUS:    return "op+";
        case TOK_MINUS:   return "op-";
        case TOK_STAR:    return "op*";
        case TOK_SLASH:   return "op/";
        case TOK_PERCENT: return "op%";
        case TOK_EQ:      return "op==";
        case TOK_NE:      return "op!=";
        case TOK_LT:      return "op<";
        case TOK_LE:      return "op<=";
        case TOK_GT:      return "op>";
        case TOK_GE:      return "op>=";
        case TOK_BAND:    return "op&";
        case TOK_BOR:     return "op|";
        case TOK_BXOR:    return "op^";
        case TOK_BNOT:    return "op~";
        case TOK_SHL:     return "op<<";
        case TOK_SHR:     return "op>>";
        case TOK_HASH: case TOK_LEN: return "op_len";
        case TOK_IN:      return "op_in";
        case TOK_IS:      return "op_is";
        case TOK_AS: case TOK_TYPE_I32: case TOK_TYPE_I64: case TOK_TYPE_F32: case TOK_TYPE_F64: case TOK_TYPE_BOOL:
            return "op_as";
        case TOK_TYPE_STRING: return "op_str";
        default: break;
    }
    if (vm_str_eq_cstr(k->start, k->length, "iter") == VM_TRUE) return "op_iter";
    gen_error(g, fn_node, "Unsupported operator in operator overload");
    return "op+";
}

/* `op_as` is stored per target type: op_as_i32, op_as_f32, op_str, op_as_<Struct>, ... */
static void op_as_name(Compiler *c, char *out, vm_u32 cap, VMDataType rtype, int rsid) {
    if (rsid >= 0) {
        const char *sn = c->structs[rsid].name;
        vm_u32 sl = vm_str_len(sn);
        vm_str_copy(out, "op_as_", 6, cap);
        vm_u32 k = 0;
        for (; k < sl && (6 + k) < cap - 1; k++) out[6 + k] = sn[k];
        out[6 + k] = '\0';
    } else if (rtype == VM_TYPE_STRING) vm_str_copy(out, "op_str", 6, cap);
    else if (rtype == VM_TYPE_I64)  vm_str_copy(out, "op_as_i64", 9, cap);
    else if (rtype == VM_TYPE_F32)  vm_str_copy(out, "op_as_f32", 9, cap);
    else if (rtype == VM_TYPE_F64)  vm_str_copy(out, "op_as_f64", 9, cap);
    else if (rtype == VM_TYPE_BOOL) vm_str_copy(out, "op_as_bool", 10, cap);
    else                            vm_str_copy(out, "op_as_i32", 9, cap);
}

/*
 * `@[intrinsic("name")] func F(...): R` (stdlib only): F's body is the intrinsic's opcodes,
 * or F is the native `$name` (images re-bind natives by that name) with F as an alias.
 */
static void bind_intrinsic(Gen *g, AstRef fn_node, vm_u32 fid) {
    Compiler *c = g->c;
    VMFunctionInfo *fn = &c->chunk->functions[fid];
    const char *iname = c->pending_intrinsic;
    const VMIntrinsic *it = vm_intrinsic_find(iname, vm_str_len(iname));
    if (it == VM_NULL) {
        gen_error(g, fn_node, "Unknown intrinsic");
        return;
    }
    if (it->arity != fn->arity) {
        gen_error(g, fn_node, "Intrinsic declared with the wrong number of parameters");
        return;
    }
    if (c_check_duplicate_func_overload(c, (int)fid)) (void)c_check_foreign_overload(c, (int)fid);
    if (it->num_ops > 0) {
        fn->start_ip = c->chunk->code_count;
        for (vm_u8 k = 0; k < it->num_ops; k++) vm_chunk_emit(c->chunk, it->ops[k]);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        fn->num_regs = (fn->arity > 0) ? fn->arity : 1;
        g->inline_intr[fid] = it;
        return;
    }
    char alias[64];
    vm_str_copy(alias, fn->name, vm_str_len(fn->name), 64);
    fn->name[0] = '$';
    vm_str_copy(fn->name + 1, iname, vm_str_len(iname), 63);
    c_func_index_rename(c, (vm_u32)fid);
    fn->native_fn = it->native;
    fn->start_ip = c->chunk->code_count;
    fn->num_regs = (fn->arity > 0) ? fn->arity : 1;
    c_register_alias(c, alias, (vm_i32)fid);
}

/* `@[deprecated("...")]`, `@[host_export("...")]` apply to the next compiled function */
static void apply_attrs(Gen *g, AstList attrs) {
    Compiler *c = g->c;
    for (vm_u32 i = 0; i < LLEN(g, attrs); i++) {
        AstRef at = LAT(g, attrs, i);
        AstList args = ND(g, at)->a;
        const Token *name = gen_tok(g, ND(g, at)->tok);
        const Token *arg0 = (LLEN(g, args) > 0 && ND(g, LAT(g, args, 0))->kind == AST_STRING)
                            ? gen_tok(g, ND(g, LAT(g, args, 0))->tok) : VM_NULL;
        if (vm_str_eq_cstr(name->start, name->length, "deprecated") == VM_TRUE) {
            c->pending_deprecated = VM_TRUE;
            vm_str_copy(c->pending_deprecated_msg, "Function is marked @[deprecated]", 32, 64);
            if (arg0 != VM_NULL) vm_str_copy(c->pending_deprecated_msg, arg0->start, arg0->length, 64);
        } else if (vm_str_eq_cstr(name->start, name->length, "intrinsic") == VM_TRUE) {
            if (c->in_std_module == VM_FALSE) {
                gen_error(g, at, "@[intrinsic] is only available inside the standard library");
            } else if (arg0 == VM_NULL || arg0->length == 0) {
                gen_error(g, at, "@[intrinsic] needs a name: @[intrinsic(\"str_len\")]");
            } else {
                vm_str_copy(c->pending_intrinsic, arg0->start, arg0->length, 32);
            }
        } else if (vm_str_eq_cstr(name->start, name->length, "host_export") == VM_TRUE && arg0 != VM_NULL) {
            vm_str_copy(c->pending_host_export, arg0->start, arg0->length, 64);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Parameters
 * ------------------------------------------------------------------------- */

static vm_bool type_is_func_node(Gen *g, AstRef ty) {
    return vm_bool_from_int(ty != 0 && ND(g, ty)->kind == AST_TY_FUNC);
}

/* Declares one explicitly-typed parameter; returns VM_FALSE when the type is missing */
static void declare_typed_param(Gen *g, VMFunctionInfo *fn, AstRef prm, VMDataType ptype, int psid,
                                VMDataType pelem, int pfunc_tid) {
    Compiler *c = g->c;
    vm_u8 pidx = fn->arity;
    vm_u32 fid = (vm_u32)(fn - c->chunk->functions);
    const Token *pn = gen_tok(g, ND(g, prm)->tok);
    if (fid < VM_MAX_FUNCTIONS && pidx < VM_MAX_PARAMS) {
        g->param_name[fid][pidx] = pn->start;
        g->param_len[fid][pidx] = pn->length;
        fn->param_func_type[pidx] = (vm_i16)pfunc_tid;
    }
    c_declare_param(c, fn, gen_token(g, ND(g, prm)->tok), ptype, psid);
    if (ptype == VM_TYPE_ARRAY && psid < 0 && pelem != VM_TYPE_VOID) {
        LocalVar *plv = &c->locals[c->num_locals - 1];
        if (plv->elem_count == 0) plv->elem_count = 1;
        plv->elem_types[0] = pelem;
    }
    if (pfunc_tid >= 0) c->locals[c->num_locals - 1].func_type_id = pfunc_tid;
    if (ND(g, prm)->b != 0) {
        fn->param_has_default[pidx] = VM_TRUE;
        fn->param_defaults[pidx] = const_value(g, ND(g, prm)->b, ptype, VM_NULL);
    } else {
        fn->min_arity++;
    }
}

/* Captured parameters are boxed on entry (must run after start_ip is set) */
static void box_captured_params(Gen *g) {
    Compiler *c = g->c;
    for (int i = 0; i < c->num_locals; i++) {
        if (gen_should_box(g, c->locals[i].name, c->locals[i].length)) gen_box_local(g, i);
    }
}

/* ---------------------------------------------------------------------------
 * Functions
 * ------------------------------------------------------------------------- */

static int register_template(Gen *g, const char *name, vm_u32 len, AstRef fn_node, const char *prefix, int struct_id) {
    Compiler *c = g->c;
    if (c->num_generics >= 64) {
        gen_error(g, fn_node, "Too many generic functions");
        return -1;
    }
    int gid = c->num_generics++;
    GenericTemplate *gt = &c->generics[gid];
    vm_str_copy(gt->name, name, len, 64);
    gt->num_type_params = 0;
    gt->is_variadic = VM_FALSE;
    gt->body_src_start = VM_NULL;
    gt->module_idx = c->cur_module_idx;
    AstList gens = AST_FUNC_GENERICS(g->t, fn_node);
    for (vm_u32 i = 0; i < LLEN(g, gens) && gt->num_type_params < 8; i++) {
        const Token *tp = gen_tok(g, LAT(g, gens, i));
        vm_str_copy(gt->type_param_names[gt->num_type_params++], tp->start, tp->length, 32);
    }
    g->templates[gid].t = g->t;
    g->templates[gid].fn = fn_node;
    vm_str_copy(g->templates[gid].prefix, prefix ? prefix : "", prefix ? vm_str_len(prefix) : 0, 64);
    g->templates[gid].struct_id = struct_id;
    /* Qualified name so `Struct.f<T>` / `mod.f<T>` resolve like other functions */
    if (prefix != VM_NULL && prefix[0] != '\0') vm_str_join_dot(gt->name, 64, prefix, vm_str_len(prefix), name, len);
    return gid;
}

/*
 * Compiles a named function / method / operator. `prefix` is the struct or module
 * namespace ("" for none); `custom_name` overrides the node's name (operators,
 * contract default methods).
 */
int gen_function(Gen *g, AstRef fn_node, const char *prefix, const char *custom_name, vm_bool is_pub) {
    Compiler *c = g->c;
    int saved_struct_id = c->current_struct_id;
    gen_at(g, fn_node);

    const char *raw_name;
    vm_u32 raw_len;
    if (custom_name != VM_NULL) {
        raw_name = custom_name;
        raw_len = vm_str_len(custom_name);
    } else {
        const Token *nt = gen_tok(g, ND(g, fn_node)->tok);
        raw_name = nt->start;
        raw_len = nt->length;
    }

    /* Out-of-line method: `func Vec2.scale(...)` / `operator Vec2.+ (...)` */
    AstList owner = AST_FUNC_OWNER(g->t, fn_node);
    if (LLEN(g, owner) > 0) {
        const Token *ot = gen_tok(g, LAT(g, owner, LLEN(g, owner) - 1));
        int out_sid = c_resolve_struct(c, ot->start, ot->length);
        if (out_sid < 0) {
            gen_error(g, fn_node, "Unknown struct in out-of-line method declaration");
            return -1;
        }
        c->current_struct_id = out_sid;
        prefix = c->structs[out_sid].name;
    }

    if (custom_name == VM_NULL && LLEN(g, AST_FUNC_GENERICS(g->t, fn_node)) > 0) {
        (void)register_template(g, raw_name, raw_len, fn_node, prefix, c->current_struct_id);
        c->current_struct_id = saved_struct_id;
        return -1;
    }

    apply_attrs(g, AST_FUNC_ATTRS(g->t, fn_node));

    FuncState *st = (FuncState *)c->chunk->realloc_fn(c->chunk->alloc_ud, VM_NULL, 0, sizeof(FuncState));
    if (st == VM_NULL) return -1;
    save_state(g, st);

    vm_u32 fid = c_alloc_func_slot(c);
    VMFunctionInfo *fn = &c->chunk->functions[fid];
    if (prefix != VM_NULL && prefix[0] != '\0') vm_str_join_dot(fn->name, 64, prefix, vm_str_len(prefix), raw_name, raw_len);
    else vm_str_copy(fn->name, raw_name, raw_len, 64);

    init_function(fn);
    /* Inside an imported module only `pub` items are shared; the main program keeps everything visible */
    if (c->cur_module_idx >= 0 && custom_name == VM_NULL && (ND(g, fn_node)->flags & AST_F_PUB) == 0) is_pub = VM_FALSE;
    fn->is_pub = is_pub;
    fn->is_deprecated = c->pending_deprecated;
    if (c->pending_deprecated == VM_TRUE) vm_str_copy(fn->deprecated_msg, c->pending_deprecated_msg, vm_str_len(c->pending_deprecated_msg), 64);
    if (c->pending_host_export[0] != '\0') {
        c_register_alias(c, fn->name, (vm_i32)fid);
        vm_str_copy(fn->name, c->pending_host_export, vm_str_len(c->pending_host_export), 64);
        c_func_index_rename(c, fid);
    }
    c->pending_deprecated = VM_FALSE;
    c->pending_deprecated_msg[0] = '\0';
    c->pending_host_export[0] = '\0';
    fn->owner_struct_id = (vm_i16)c->current_struct_id;

    c->num_locals = 0; c->num_outer_locals = 0; c->scope_depth = 1; c->next_reg = 0; c->max_reg = 0;
    g->num_defers = 0;
    c->num_breaks = 0; c->num_continues = 0;
    gen_fn_push(g, VM_NULL, (int)fid, ND(g, fn_node)->b);

    AstList params = ND(g, fn_node)->a;
    vm_u8 peek_arity = (vm_u8)LLEN(g, params);
    vm_u32 fn_name_len = vm_str_len(fn->name);

    /* Forward prototype (`type func name(...)`) with the same name and arity */
    int proto_fid = -1;
    for (int i = c_func_first_named(c, fn->name, fn_name_len); i >= 0 && i < (int)fid;
         i = c_func_next_named(c, i, fn->name, fn_name_len)) {
        VMFunctionInfo *ef = &c->chunk->functions[i];
        if (ef->start_ip == 0xFFFFFFFFu && ef->arity == peek_arity &&
            vm_str_eq(ef->name, vm_str_len(ef->name), fn->name, fn_name_len) == VM_TRUE) {
            proto_fid = i;
            break;
        }
    }
    /* Signature required by a `type` contract this struct implements (parameter / return inference) */
    TypeContract *proto_tc = VM_NULL;
    vm_u8 proto_rm = 0;
    if (c->current_struct_id >= 0) {
        StructDef *csd = &c->structs[c->current_struct_id];
        for (vm_u8 t = 0; t < csd->num_implemented_types && proto_tc == VM_NULL; t++) {
            TypeContract *tc = &c->type_contracts[csd->implemented_types[t]];
            for (vm_u8 rm = 0; rm < tc->num_req_methods; rm++) {
                vm_u32 rmlen = vm_str_len(tc->req_method_names[rm]);
                if (vm_str_eq(tc->req_method_names[rm], rmlen, raw_name, raw_len) == VM_TRUE && tc->req_method_arity[rm] == peek_arity) {
                    proto_tc = tc;
                    proto_rm = rm;
                    break;
                }
            }
        }
    }

    for (vm_u32 i = 0; i < LLEN(g, params) && !c->had_error; i++) {
        AstRef prm = LAT(g, params, i);
        AstRef pty = ND(g, prm)->a;
        vm_u8 pidx = fn->arity;
        int psid = -1;
        int pfunc_tid = -1;
        VMDataType ptype = VM_TYPE_I32;
        VMDataType pelem = VM_TYPE_VOID;
        const Token *pname = gen_tok(g, ND(g, prm)->tok);

        if (pty != 0) {
            ptype = gen_type(g, pty, &psid, &pelem, &pfunc_tid);
        } else if (proto_fid >= 0 && pidx < c->chunk->functions[proto_fid].arity) {
            ptype = c->chunk->functions[proto_fid].param_types[pidx];
            psid = c->chunk->functions[proto_fid].param_struct_ids[pidx];
        } else if (proto_tc != VM_NULL && pidx < proto_tc->req_method_arity[proto_rm]) {
            ptype = proto_tc->req_method_param_types[proto_rm][pidx];
            psid = (proto_tc->req_method_param_is_self[proto_rm][pidx] == VM_TRUE)
                   ? c->current_struct_id : proto_tc->req_method_param_sids[proto_rm][pidx];
            if (psid >= 0 && c->structs[psid].num_fields > 0) ptype = c->structs[psid].field_types[0];
        } else if (pidx == 0 && c->current_struct_id >= 0 && vm_str_eq_cstr(pname->start, pname->length, "self") == VM_TRUE) {
            const char *sname = c->structs[c->current_struct_id].name;
            vm_u32 slen = vm_str_len(sname);
            vm_bool is_prim = VM_FALSE;
            for (int pt = 0; pt < VM_TYPE_COUNT; pt++) {
                const char *prname = c->chunk->primitive_struct_name[pt];
                if (prname[0] != '\0' && vm_str_eq(sname, slen, prname, vm_str_len(prname)) == VM_TRUE) {
                    psid = -1;
                    ptype = (VMDataType)pt;
                    is_prim = VM_TRUE;
                    break;
                }
            }
            if (is_prim == VM_FALSE) {
                psid = c->current_struct_id;
                ptype = (c->structs[psid].num_fields > 0) ? c->structs[psid].field_types[0] : VM_TYPE_I32;
            }
        } else {
            gen_error(g, prm, "Expected ':' (parameter type)");
            break;
        }
        declare_typed_param(g, fn, prm, ptype, psid, pelem, pfunc_tid);
    }

    /* Refine the prototype match by exact parameter types (overloaded prototypes) */
    for (int i = c_func_first_named(c, fn->name, fn_name_len); i >= 0 && i < (int)fid;
         i = c_func_next_named(c, i, fn->name, fn_name_len)) {
        VMFunctionInfo *ef = &c->chunk->functions[i];
        if (ef->start_ip != 0xFFFFFFFFu || ef->arity != fn->arity) continue;
        if (vm_str_eq(ef->name, vm_str_len(ef->name), fn->name, fn_name_len) == VM_FALSE) continue;
        vm_bool exact = VM_TRUE;
        for (vm_u8 p = 0; p < fn->arity; p++) {
            if (ef->param_types[p] != fn->param_types[p] || ef->param_struct_ids[p] != fn->param_struct_ids[p]) {
                exact = VM_FALSE;
                break;
            }
        }
        if (exact == VM_TRUE) {
            proto_fid = i;
            break;
        }
    }

    AstRef ret = ND(g, fn_node)->c;
    if (ret != 0) {
        if (type_is_func_node(g, ret)) {
            fn->return_func_type_id = (vm_i16)gen_func_type(g, ret);
            fn->return_type = VM_TYPE_I32;
            fn->return_elem_types[0] = VM_TYPE_I32;
            fn->return_reg_count = 1;
        } else if (ND(g, ret)->kind == AST_TY_TUPLE) {
            AstList elems = ND(g, ret)->a;
            vm_u8 rcnt = 0;
            for (vm_u32 i = 0; i < LLEN(g, elems) && rcnt < 8; i++) {
                fn->return_elem_types[rcnt++] = gen_type(g, LAT(g, elems, i), VM_NULL, VM_NULL, VM_NULL);
            }
            fn->return_type = (rcnt > 0) ? fn->return_elem_types[0] : VM_TYPE_VOID;
            fn->return_reg_count = (rcnt > 0) ? rcnt : 1;
        } else {
            int rsid = -1, rftid = -1;
            VMDataType relem = VM_TYPE_VOID;
            fn->return_type = gen_type(g, ret, &rsid, &relem, &rftid);
            g->ret_elem[fid] = relem;
            if (rftid >= 0) fn->return_func_type_id = (vm_i16)rftid;
            fn->return_elem_types[0] = fn->return_type;
            fn->return_struct_id = (vm_i16)rsid;
            if (rsid >= 0) {
                fn->return_reg_count = c->structs[rsid].num_fields;
                for (vm_u8 f = 0; f < c->structs[rsid].num_fields; f++) fn->return_elem_types[f] = c->structs[rsid].field_types[f];
            }
        }
    } else if (proto_fid >= 0) {
        VMFunctionInfo *pf = &c->chunk->functions[proto_fid];
        fn->return_type = pf->return_type;
        fn->return_struct_id = pf->return_struct_id;
        fn->return_func_type_id = pf->return_func_type_id;
        fn->return_reg_count = pf->return_reg_count;
        for (vm_u8 r = 0; r < pf->return_reg_count && r < 8; r++) fn->return_elem_types[r] = pf->return_elem_types[r];
    } else if (proto_tc != VM_NULL) {
        fn->return_type = proto_tc->req_method_ret[proto_rm];
        int rsid = (proto_tc->req_method_ret_is_self[proto_rm] == VM_TRUE) ? c->current_struct_id : proto_tc->req_method_ret_sid[proto_rm];
        fn->return_struct_id = (vm_i16)rsid;
        if (rsid >= 0) {
            if (c->structs[rsid].num_fields > 0) fn->return_type = c->structs[rsid].field_types[0];
            fn->return_reg_count = c->structs[rsid].num_fields;
        }
        fn->return_elem_types[0] = fn->return_type;
    }

    if (custom_name != VM_NULL && vm_str_eq_cstr(custom_name, vm_str_len(custom_name), "op_as") == VM_TRUE) {
        char as_name[48];
        op_as_name(c, as_name, 48, fn->return_type, fn->return_struct_id);
        if (prefix != VM_NULL && prefix[0] != '\0') vm_str_join_dot(fn->name, 64, prefix, vm_str_len(prefix), as_name, vm_str_len(as_name));
        else vm_str_copy(fn->name, as_name, vm_str_len(as_name), 64);
        c_func_index_rename(c, fid);
        fn_name_len = vm_str_len(fn->name);
    }

    AstRef body = ND(g, fn_node)->b;
    if (g->proto_pass) body = 0;   /* signature now, body in gen_unit's second pass */
    if (body == 0 && c->pending_intrinsic[0] != '\0') {
        bind_intrinsic(g, fn_node, fid);
        c->pending_intrinsic[0] = '\0';
        gen_fn_pop(g);
        restore_state(g, st);
        c->chunk->realloc_fn(c->chunk->alloc_ud, st, sizeof(FuncState), 0);
        c->current_struct_id = saved_struct_id;
        return (int)fid;
    }
    if (body == 0 && g->proto_pass && proto_fid >= 0 && (vm_u32)proto_fid != fid) {
        /* Already declared by a written prototype: keep that one */
        c->chunk->num_functions--;
        gen_fn_pop(g);
        restore_state(g, st);
        c->chunk->realloc_fn(c->chunk->alloc_ud, st, sizeof(FuncState), 0);
        c->current_struct_id = saved_struct_id;
        return proto_fid;
    }
    if (body == 0) {
        /* Prototype / signature only */
        fn->start_ip = 0xFFFFFFFFu;
        fn->num_regs = c->max_reg;
        if (c_check_duplicate_func_overload(c, (int)fid)) (void)c_check_foreign_overload(c, (int)fid);
        gen_fn_pop(g);
        restore_state(g, st);
        c->chunk->realloc_fn(c->chunk->alloc_ud, st, sizeof(FuncState), 0);
        c->current_struct_id = saved_struct_id;
        return (int)fid;
    }

    /* Implementing an earlier prototype: reuse its slot so existing calls bind to this body */
    int reuse_fid = -1;
    for (int i = c_func_first_named(c, fn->name, fn_name_len); i >= 0 && i < (int)fid;
         i = c_func_next_named(c, i, fn->name, fn_name_len)) {
        VMFunctionInfo *ef = &c->chunk->functions[i];
        if (ef->start_ip != 0xFFFFFFFFu || ef->arity != fn->arity) continue;
        if (vm_str_eq(ef->name, vm_str_len(ef->name), fn->name, fn_name_len) == VM_FALSE) continue;
        vm_bool same = VM_TRUE;
        for (vm_u8 p = 0; p < fn->arity; p++) {
            if (ef->param_types[p] != fn->param_types[p] || ef->param_struct_ids[p] != fn->param_struct_ids[p]) {
                same = VM_FALSE;
                break;
            }
        }
        if (same == VM_TRUE) {
            reuse_fid = i;
            break;
        }
    }
    if (reuse_fid >= 0) {
        c->chunk->functions[reuse_fid] = *fn;
        for (int pi = 0; pi < VM_MAX_PARAMS; pi++) {
            g->param_name[reuse_fid][pi] = g->param_name[fid][pi];
            g->param_len[reuse_fid][pi] = g->param_len[fid][pi];
        }
        g->ret_elem[reuse_fid] = g->ret_elem[fid];
        c->chunk->num_functions--;
        fid = (vm_u32)reuse_fid;
        fn = &c->chunk->functions[fid];
    } else {
        if (c_check_duplicate_func_overload(c, (int)fid)) (void)c_check_foreign_overload(c, (int)fid);
    }

    c->current_func_id = (int)fid;
    c->current_ret_type = fn->return_type;
    c->current_ret_struct_id = fn->return_struct_id;
    fn->start_ip = c->chunk->code_count;

    if ((prefix == VM_NULL || prefix[0] == '\0') && custom_name == VM_NULL &&
        vm_str_eq_cstr(raw_name, raw_len, "main") == VM_TRUE) {
        c->chunk->entry_func_id = (vm_i32)fid;
    }

    g->fns[g->fn_depth - 1].fid = (int)fid;
    box_captured_params(g);
    gen_block(g, body);
    gen_at(g, fn_node);
    if (c->chunk->entry_func_id == (vm_i32)fid) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_HALT, 0, 0, 0));
    else if (gen_sid_is_result(g, c->chunk->functions[fid].return_struct_id) &&
             c->structs[c->chunk->functions[fid].return_struct_id].res_val_w == 0) {
        /* Falling off the end of a `void ! E` function is success (block defers already ran) */
        ExprResult okv = gen_ok_void(g, c->chunk->functions[fid].return_struct_id);
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, okv.reg, c->structs[okv.struct_id].num_fields, 0));
    } else vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, 0, c->chunk->functions[fid].return_reg_count, 0));
    fn = &c->chunk->functions[fid];
    fn->num_regs = c->max_reg;

    /* Bind matching `type` layout forward methods (`VecOps.<name>`) to this implementation */
    if (c->current_struct_id >= 0 && c->structs[c->current_struct_id].owner_type_id < 0) {
        int impl_sid = c->current_struct_id;
        for (int t = 0; t < c->num_type_contracts; t++) {
            TypeContract *tc = &c->type_contracts[t];
            if (tc->layout_struct_id < 0) continue;
            char lqname[64];
            vm_str_join_dot(lqname, 64, tc->name, vm_str_len(tc->name), raw_name, raw_len);
            vm_u32 lqlen = vm_str_len(lqname);
            for (vm_u32 f = 0; f < c->chunk->num_functions; f++) {
                VMFunctionInfo *lf = &c->chunk->functions[f];
                if (lf->start_ip == 0xFFFFFFFFu && lf->arity == fn->arity && vm_str_eq(lf->name, vm_str_len(lf->name), lqname, lqlen) == VM_TRUE) {
                    lf->start_ip = fn->start_ip;
                    lf->num_regs = fn->num_regs;
                }
            }
            if (tc->last_impl_struct_id < 0 && c_does_struct_implement_type(c, impl_sid, t) == VM_TRUE) {
                tc->last_impl_struct_id = impl_sid;
            }
        }
    }

    gen_fn_pop(g);
    restore_state(g, st);
    c->chunk->realloc_fn(c->chunk->alloc_ud, st, sizeof(FuncState), 0);
    c->current_struct_id = saved_struct_id;
    return (int)fid;
}

/*
 * Body of a trailing block: like a function body, but a final expression statement is the
 * block's value (`{ x -> x * 2 }`). With `infer`, the return type comes from that expression.
 */
static void gen_block_value_body(Gen *g, AstRef block, int fid, vm_bool infer) {
    Compiler *c = g->c;
    AstList stmts = ND(g, block)->a;
    vm_u32 n = LLEN(g, stmts);
    gen_begin_scope(g);
    for (vm_u32 i = 0; i + 1 < n && !c->had_error; i++) gen_stmt(g, LAT(g, stmts, i));
    if (n > 0 && !c->had_error) {
        AstRef last = LAT(g, stmts, n - 1);
        VMFunctionInfo *fn = &c->chunk->functions[fid];
        vm_bool wants_value = vm_bool_from_int(infer || fn->return_type != VM_TYPE_VOID);
        if (wants_value && ND(g, last)->kind == AST_EXPR_STMT) {
            gen_at(g, last);
            ExprResult v = gen_expr(g, ND(g, last)->a, infer ? GEN_ANY : fn->return_type);
            fn = &c->chunk->functions[fid];
            if (infer && v.type != VM_TYPE_VOID) {
                fn->return_type = v.type;
                fn->return_elem_types[0] = v.type;
                c->current_ret_type = v.type;
            }
            if (fn->return_type != VM_TYPE_VOID) {
                v = gen_convert(g, v, fn->return_type);
                gen_emit_defers_for_return(g);
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, v.reg, 1, 0));
            }
        } else {
            gen_stmt(g, last);
        }
    }
    gen_end_scope(g);
}

/* Lambda / nested function: compiled inline behind a jump; the enclosing locals become capturable */
ExprResult gen_lambda(Gen *g, AstRef fn_node) {
    Compiler *c = g->c;
    static int lambda_counter = 0;
    lambda_counter++;
    ExprResult res = gen_result(VM_TYPE_I32);

    vm_bool has_name = vm_bool_from_int((ND(g, fn_node)->flags & (AST_F_NAMED | AST_F_NESTED)) != 0);
    char lname[64];
    char numbuf[16];
    vm_u32 nlen = vm_i64_to_str(numbuf, lambda_counter);
    vm_u32 p = 0;
    const Token *name_tok = gen_tok(g, ND(g, fn_node)->tok);
    if (has_name) {
        const char *pfx = "$nest_";
        for (vm_u32 i = 0; pfx[i]; i++) lname[p++] = pfx[i];
        for (vm_u32 i = 0; i < nlen && p < 48; i++) lname[p++] = numbuf[i];
        lname[p++] = '_';
        for (vm_u32 i = 0; i < name_tok->length && p < 63; i++) lname[p++] = name_tok->start[i];
    } else {
        const char *pfx = "$lam";
        for (vm_u32 i = 0; pfx[i]; i++) lname[p++] = pfx[i];
        for (vm_u32 i = 0; i < nlen && p < 63; i++) lname[p++] = numbuf[i];
    }
    lname[p] = '\0';

    if (LLEN(g, AST_FUNC_GENERICS(g->t, fn_node)) > 0) {
        res.lambda_gen_id = register_template(g, lname, p, fn_node, "", -1);
        return res;
    }

    vm_u32 skip_jmp = gen_emit_jump(g, OP_JMP, 0);

    FuncState *st = (FuncState *)c->chunk->realloc_fn(c->chunk->alloc_ud, VM_NULL, 0, sizeof(FuncState));
    if (st == VM_NULL) return res;
    save_state(g, st);

    /* Expose the enclosing function's locals for closure capture */
    c->num_outer_locals = (st->num_locals <= 128) ? st->num_locals : 128;
    for (int i = 0; i < c->num_outer_locals; i++) c->outer_locals[i] = st->locals[i];

    vm_u32 fid = c_alloc_func_slot(c);
    VMFunctionInfo *fn = &c->chunk->functions[fid];
    vm_str_copy(fn->name, lname, p, 64);
    init_function(fn);

    c->num_locals = 0; c->scope_depth = 1; c->next_reg = 0; c->max_reg = 0;
    g->num_defers = 0;
    c->num_breaks = 0; c->num_continues = 0;
    gen_fn_push(g, st, (int)fid, ND(g, fn_node)->b);

    vm_bool is_block = vm_bool_from_int((ND(g, fn_node)->flags & AST_F_BLOCK) != 0);
    int block_ftid = is_block ? g->block_ftid : -1;
    g->block_ftid = -1;
    g->elem_hint = GEN_ANY;
    TypeContract *btc = (block_ftid >= 0) ? &c->type_contracts[block_ftid] : VM_NULL;

    AstList params = ND(g, fn_node)->a;
    if (is_block && btc != VM_NULL && LLEN(g, params) != btc->func_arity && LLEN(g, params) != 0) {
        gen_error(g, fn_node, "Block parameter count does not match the callback type");
    }
    for (vm_u32 i = 0; i < LLEN(g, params) && !c->had_error; i++) {
        AstRef prm = LAT(g, params, i);
        int psid = -1, pftid = -1;
        VMDataType pelem = VM_TYPE_VOID;
        VMDataType ptype;
        if (ND(g, prm)->a != 0) {
            ptype = gen_type(g, ND(g, prm)->a, &psid, &pelem, &pftid);
        } else if (btc != VM_NULL && i < btc->func_arity) {
            ptype = btc->func_param_types[i];
            psid = btc->func_param_sids[i];
        } else {
            gen_error(g, prm, is_block ? "Cannot infer block parameter type: the callee has no matching callback parameter"
                                       : "Expected ':' (parameter type)");
            break;
        }
        declare_typed_param(g, fn, prm, ptype, psid, pelem, pftid);
    }
    fn = &c->chunk->functions[fid];

    AstRef ret = ND(g, fn_node)->c;
    if (ret == 0 && btc != VM_NULL) {
        fn->return_type = btc->func_ret_type;
        fn->return_struct_id = (vm_i16)btc->func_ret_sid;
        if (btc->func_ret_sid >= 0) fn->return_reg_count = c->structs[btc->func_ret_sid].num_fields;
        fn->return_elem_types[0] = fn->return_type;
    } else if (ret != 0) {
        int rsid = -1, rftid = -1;
        if (type_is_func_node(g, ret)) {
            fn->return_func_type_id = (vm_i16)gen_func_type(g, ret);
            fn->return_type = VM_TYPE_I32;
        } else {
            VMDataType relem = VM_TYPE_VOID;
            fn->return_type = gen_type(g, ret, &rsid, &relem, &rftid);
            g->ret_elem[fid] = relem;
            if (rftid >= 0) fn->return_func_type_id = (vm_i16)rftid;
            fn->return_struct_id = (vm_i16)rsid;
            if (rsid >= 0) fn->return_reg_count = c->structs[rsid].num_fields;
        }
        fn->return_elem_types[0] = fn->return_type;
    }
    c->current_ret_type = fn->return_type;
    c->current_ret_struct_id = fn->return_struct_id;
    c->current_func_id = (int)fid;
    fn->start_ip = c->chunk->code_count;

    box_captured_params(g);
    if (has_name) {
        /* Self-recursion by name: a direct call keeps the current closure (see OP_CALL) */
        c_declare_local(c, *name_tok, VM_TYPE_I32, VM_TRUE, (VMReg){0});
        c->locals[c->num_locals - 1].lambda_func_id = (int)fid;
    }

    if (is_block) gen_block_value_body(g, ND(g, fn_node)->b, (int)fid, vm_bool_from_int(btc == VM_NULL));
    else gen_block(g, ND(g, fn_node)->b);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, 0, c->chunk->functions[fid].return_reg_count, 0));
    c->chunk->functions[fid].num_regs = c->max_reg;
    gen_finish_closure_info(g, (int)fid);
    gen_fn_pop(g);

    restore_state(g, st);
    c->chunk->realloc_fn(c->chunk->alloc_ud, st, sizeof(FuncState), 0);
    gen_patch_jump(g, skip_jmp);

    res.reg = c_alloc_reg(c);
    res.lambda_func_id = (int)fid;
    if (c->chunk->functions[fid].num_upvals > 0) {
        /* Captures something: build a closure object in the enclosing frame */
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_CLOSURE, res.reg, (vm_u16)fid));
        res.is_closure = VM_TRUE;
    } else {
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, res.reg, (vm_i16)fid));
    }
    if (c->chunk->functions[fid].return_func_type_id >= 0) EXPR_SET_FUNC_TYPE(res, c->chunk->functions[fid].return_func_type_id);
    return res;
}

static void type_label(Compiler *c, VMDataType t, int sid, char *out, vm_u32 *k, vm_u32 cap) {
    const char *tn = (sid >= 0) ? c->structs[sid].name : vm_type_name(t);
    for (vm_u32 i = 0; tn[i] && *k + 1 < cap; i++) out[(*k)++] = tn[i];
}

/*
 * `f<T...>`: compiles the template with its type parameters bound (once per distinct set of
 * type arguments; later calls reuse the instance). The body is compiled by gen_function, so
 * an instance supports everything a normal function does (structs, T?, T ! E, defaults...).
 */
int gen_instantiate_generic(Gen *g, int gen_id, const VMDataType *types, const int *sids, vm_u8 num_types, AstRef at) {
    Compiler *c = g->c;
    GenericTemplate *gt = &c->generics[gen_id];
    if (num_types != gt->num_type_params) {
        gen_error(g, at, "Wrong number of type arguments for generic function");
        return -1;
    }
    for (int i = 0; i < g->num_instances; i++) {
        GenInstance *in = &g->instances[i];
        if (in->gen_id != gen_id || in->num_types != num_types) continue;
        vm_bool same = VM_TRUE;
        for (vm_u8 k = 0; k < num_types; k++) {
            if (in->types[k] != types[k] || in->sids[k] != sids[k]) same = VM_FALSE;
        }
        if (same) return in->fid;
    }
    if (g->num_instances >= GEN_MAX_INSTANCES) {
        gen_error(g, at, "Too many generic instances");
        return -1;
    }

    /* Mangled name: `name<i64,Point>` */
    char mangled[64];
    const char *base = gt->name;
    const char *dot = VM_NULL;
    for (const char *p = base; *p; p++) if (*p == '.') dot = p;
    if (dot != VM_NULL && g->templates[gen_id].prefix[0] != '\0') base = dot + 1;
    vm_u32 k = 0;
    for (vm_u32 i = 0; base[i] && k < 40; i++) mangled[k++] = base[i];
    mangled[k++] = '<';
    for (vm_u8 i = 0; i < num_types; i++) {
        if (i > 0) mangled[k++] = ',';
        type_label(c, types[i], sids[i], mangled, &k, 62);
    }
    mangled[k++] = '>';
    mangled[k] = '\0';

    AstTree *saved_t = g->t;
    char saved_names[8][32];
    VMDataType saved_types[8];
    int saved_sids[8];
    vm_u8 saved_n = c->num_active_tparams;
    for (vm_u8 i = 0; i < 8; i++) {
        vm_str_copy(saved_names[i], c->active_tparam_names[i], vm_str_len(c->active_tparam_names[i]), 32);
        saved_types[i] = c->active_tparam_types[i];
        saved_sids[i] = c->active_tparam_sids[i];
    }
    /* The type arguments were resolved by the caller, so the caller's own parameters are no longer needed */
    c->num_active_tparams = num_types;
    for (vm_u8 i = 0; i < num_types; i++) {
        vm_str_copy(c->active_tparam_names[i], gt->type_param_names[i], vm_str_len(gt->type_param_names[i]), 32);
        c->active_tparam_types[i] = types[i];
        c->active_tparam_sids[i] = sids[i];
    }

    int saved_struct = c->current_struct_id;
    c->current_struct_id = g->templates[gen_id].struct_id;
    g->t = g->templates[gen_id].t;
    vm_u32 skip_jmp = gen_emit_jump(g, OP_JMP, 0);
    /* An instance belongs to the module that declared the template: its privacy and its bare names */
    int saved_module = c->cur_module_idx;
    char saved_prefix[32];
    vm_str_copy(saved_prefix, c->module_prefix, vm_str_len(c->module_prefix), 32);
    c->cur_module_idx = gt->module_idx;
    if (gt->module_idx >= 0) {
        vm_u32 hl = vm_str_len(c->loaded_modules[gt->module_idx]);
        if (hl > 30) hl = 30;
        vm_str_copy(c->module_prefix, c->loaded_modules[gt->module_idx], hl, 32);
        c->module_prefix[hl] = '.';
        c->module_prefix[hl + 1] = '\0';
    }
    int fid = gen_function(g, g->templates[gen_id].fn, g->templates[gen_id].prefix, mangled, VM_TRUE);
    c->cur_module_idx = saved_module;
    vm_str_copy(c->module_prefix, saved_prefix, vm_str_len(saved_prefix), 32);
    gen_patch_jump(g, skip_jmp);
    g->t = saved_t;
    c->current_struct_id = saved_struct;

    c->num_active_tparams = saved_n;
    for (vm_u8 i = 0; i < 8; i++) {
        vm_str_copy(c->active_tparam_names[i], saved_names[i], vm_str_len(saved_names[i]), 32);
        c->active_tparam_types[i] = saved_types[i];
        c->active_tparam_sids[i] = saved_sids[i];
    }
    if (fid < 0) return -1;

    GenInstance *in = &g->instances[g->num_instances++];
    in->gen_id = gen_id;
    in->num_types = num_types;
    for (vm_u8 i = 0; i < num_types; i++) {
        in->types[i] = types[i];
        in->sids[i] = sids[i];
    }
    in->fid = fid;
    return fid;
}

/* ---------------------------------------------------------------------------
 * `type` declarations
 * ------------------------------------------------------------------------- */

static int new_contract(Gen *g, const char *name, vm_u32 len) {
    Compiler *c = g->c;
    int existing = c_resolve_type_contract(c, name, len);
    if (existing >= 0) return existing;
    if (c->num_type_contracts >= 32) return 31;
    int tid = c->num_type_contracts++;
    TypeContract *tc = &c->type_contracts[tid];
    vm_str_copy(tc->name, name, len, 64);
    tc->is_alias = VM_FALSE;
    tc->is_func_type = VM_FALSE;
    tc->alias_type = VM_TYPE_I32;
    tc->alias_struct_id = -1;
    tc->layout_struct_id = -1;
    tc->last_impl_struct_id = -1;
    tc->forward_func_id = -1;
    tc->func_arity = 0;
    tc->func_ret_type = VM_TYPE_VOID;
    tc->func_ret_sid = -1;
    tc->num_req_fields = 0;
    tc->num_req_methods = 0;
    g->contract_tree[tid] = VM_NULL;
    for (int m = 0; m < 16; m++) g->contract_body[tid][m] = 0;
    return tid;
}

/* `type func name(...)` / `type name = (...) : R`: callable prototype + forward function slot */
static void gen_func_prototype_type(Gen *g, vm_u32 name_tok, AstList params, AstRef ret) {
    Compiler *c = g->c;
    const Token *nt = gen_tok(g, name_tok);
    int tid = new_contract(g, nt->start, nt->length);
    TypeContract *tc = &c->type_contracts[tid];
    tc->is_func_type = VM_TRUE;
    VMDataType ptypes[8];
    int psids[8];
    vm_u8 arity = 0;
    for (vm_u32 i = 0; i < LLEN(g, params); i++) {
        AstRef p = LAT(g, params, i);
        AstRef pty = (ND(g, p)->kind == AST_PARAM) ? ND(g, p)->a : p;
        int psid = -1;
        VMDataType pt = gen_type(g, pty, &psid, VM_NULL, VM_NULL);
        if (arity < 8) {
            ptypes[arity] = pt;
            psids[arity] = psid;
        }
        arity++;
    }
    int rsid = -1;
    VMDataType rtype = (ret != 0) ? gen_type(g, ret, &rsid, VM_NULL, VM_NULL) : VM_TYPE_VOID;
    tc->func_arity = arity;
    for (vm_u8 i = 0; i < arity && i < 8; i++) {
        tc->func_param_types[i] = ptypes[i];
        tc->func_param_sids[i] = psids[i];
    }
    tc->func_ret_type = rtype;
    tc->func_ret_sid = rsid;

    char fqname[64];
    if (c->module_prefix[0] != '\0') vm_str_join_dot(fqname, 64, c->module_prefix, vm_str_len(c->module_prefix), nt->start, nt->length);
    else vm_str_copy(fqname, nt->start, nt->length, 64);
    int existing_fid = c_resolve_func_overload(c, fqname, vm_str_len(fqname), ptypes, psids, arity);
    if (existing_fid >= 0) {
        tc->forward_func_id = existing_fid;
        return;
    }
    vm_u32 fid = c_alloc_func_slot(c);
    VMFunctionInfo *fn = &c->chunk->functions[fid];
    vm_str_copy(fn->name, fqname, vm_str_len(fqname), 64);
    init_function(fn);
    fn->arity = arity;
    fn->min_arity = arity;
    for (vm_u8 i = 0; i < arity && i < 8; i++) {
        fn->param_types[i] = ptypes[i];
        fn->param_struct_ids[i] = (vm_i16)psids[i];
        fn->param_has_default[i] = VM_FALSE;
    }
    fn->return_type = rtype;
    fn->return_struct_id = (vm_i16)rsid;
    fn->return_reg_count = (rsid >= 0) ? c->structs[rsid].num_fields : 1;
    fn->return_elem_types[0] = rtype;
    fn->start_ip = 0xFFFFFFFFu;
    fn->num_regs = arity;
    tc->forward_func_id = (int)fid;
    if (g->num_decls < 128) {
        g->decl_fid[g->num_decls] = fid;
        g->decl_line[g->num_decls] = (vm_u16)gen_tok(g, name_tok)->line;
        g->decl_tok[g->num_decls] = *gen_tok(g, name_tok);
        g->decl_file[g->num_decls] = c->chunk->current_file_id;
        g->num_decls++;
    }
}

/* Every `type func` needs its body (same name and parameter types) somewhere in the program */
static void check_declarations_defined(Gen *g) {
    Compiler *c = g->c;
    for (int i = 0; i < g->num_decls && !c->had_error; i++) {
        VMFunctionInfo *fn = &c->chunk->functions[g->decl_fid[i]];
        if (fn->start_ip != 0xFFFFFFFFu) continue;
        vm_u32 nl = vm_str_len(fn->name);
        vm_bool other = VM_FALSE;
        for (int k = c_func_first_named(c, fn->name, nl); k >= 0; k = c_func_next_named(c, k, fn->name, nl)) {
            if ((vm_u32)k != g->decl_fid[i] && c->chunk->functions[k].start_ip != 0xFFFFFFFFu) other = VM_TRUE;
        }
        c->chunk->current_file_id = g->decl_file[i];
        c->chunk->current_line = g->decl_line[i];
        c->cur = g->decl_tok[i];
        char msg[200];
        const char *tail = other ? "' is declared with 'type func' here, but its body has different parameter types"
                                 : "' is declared with 'type func' here but never defined (add its body, e.g. in a file this module imports)";
        vm_u32 k2 = 0;
        const char *parts[] = {"'", fn->name, tail};
        for (int p = 0; p < 3; p++) {
            vm_u32 pl = vm_str_len(parts[p]);
            for (vm_u32 j = 0; j < pl && k2 + 1 < sizeof(msg); j++) msg[k2++] = parts[p][j];
        }
        msg[k2] = '\0';
        c_error(c, msg);
    }
}

static vm_bool is_self_type_node(Gen *g, AstRef ty, const char *cname, vm_u32 clen) {
    if (ty == 0) return VM_TRUE;
    if (ND(g, ty)->kind != AST_TY_NAME) return VM_FALSE;
    const Token *k = gen_tok(g, ND(g, ty)->tok);
    return vm_bool_from_int(k->type == TOK_SELF_TYPE || vm_str_eq(k->start, k->length, cname, clen) == VM_TRUE);
}

static void add_req_method(Gen *g, int tid, int lsid, const char *mname, vm_u32 mlen, AstList params,
                           AstRef ret, AstRef body_fn, vm_bool is_op, const char *op_str) {
    Compiler *c = g->c;
    TypeContract *tc = &c->type_contracts[tid];
    const char *cname = tc->name;
    vm_u32 clen = vm_str_len(cname);
    if (tc->num_req_methods >= 16) return;
    vm_u8 midx = tc->num_req_methods++;
    vm_u8 arity = 0;
    for (vm_u32 i = 0; i < LLEN(g, params) && arity < 8; i++) {
        AstRef p = LAT(g, params, i);
        AstRef pty = (ND(g, p)->kind == AST_PARAM) ? ND(g, p)->a : p;
        vm_bool self_p = is_self_type_node(g, pty, cname, clen);
        int psid = -1;
        VMDataType pt = VM_TYPE_I32;
        if (pty != 0) pt = gen_type(g, pty, &psid, VM_NULL, VM_NULL);
        else psid = lsid;
        tc->req_method_param_types[midx][arity] = pt;
        tc->req_method_param_sids[midx][arity] = psid;
        tc->req_method_param_is_self[midx][arity] = self_p;
        arity++;
    }
    VMDataType rtype = VM_TYPE_VOID;
    int rsid = -1;
    vm_bool ret_self = VM_FALSE;
    if (ret != 0) {
        ret_self = is_self_type_node(g, ret, cname, clen);
        rtype = gen_type(g, ret, &rsid, VM_NULL, VM_NULL);
    }
    char final_name[32];
    if (is_op == VM_TRUE && vm_str_eq_cstr(op_str, vm_str_len(op_str), "op_as") == VM_TRUE) op_as_name(c, final_name, 32, rtype, rsid);
    else if (is_op == VM_TRUE) vm_str_copy(final_name, op_str, vm_str_len(op_str), 32);
    else vm_str_copy(final_name, mname, mlen, 32);

    vm_str_copy(tc->req_method_names[midx], final_name, vm_str_len(final_name), 32);
    tc->req_method_arity[midx] = arity;
    tc->req_method_ret[midx] = rtype;
    tc->req_method_ret_sid[midx] = rsid;
    tc->req_method_ret_is_self[midx] = ret_self;
    tc->req_method_body_src[midx] = VM_NULL;
    g->contract_body[tid][midx] = body_fn;
    if (body_fn != 0) {
        g->contract_tree[tid] = g->t;
        tc->req_method_body_src[midx] = "";   /* non-NULL marks "has default body" for shared helpers */
    }
}

/* `type Name { ... }` / `type struct Name { ... }`: contract with a backing layout struct */
static void gen_contract(Gen *g, vm_u32 name_tok, AstList members) {
    Compiler *c = g->c;
    const Token *nt = gen_tok(g, name_tok);
    int tid = new_contract(g, nt->start, nt->length);
    TypeContract *tc = &c->type_contracts[tid];

    int lsid = c->num_structs++;
    StructDef *lsd = &c->structs[lsid];
    vm_str_copy(lsd->name, nt->start, nt->length, 64);
    lsd->owner_type_id = tid;
    lsd->parent_struct_id = -1;
    lsd->num_parents = 0;
    lsd->num_implemented_types = 0;
    lsd->num_fields = 0;
    lsd->is_generic = VM_FALSE;
    lsd->is_optional = VM_FALSE;
    lsd->is_enum = VM_FALSE;
    lsd->is_result = VM_FALSE;
    lsd->num_type_params = 0;
    tc->layout_struct_id = lsid;

    int saved_struct_id = c->current_struct_id;
    c->current_struct_id = lsid;

    for (vm_u32 i = 0; i < LLEN(g, members) && !c->had_error; i++) {
        AstRef m = LAT(g, members, i);
        const Token *mt = gen_tok(g, ND(g, m)->tok);
        if (ND(g, m)->kind == AST_FUNC) {
            vm_bool is_op = vm_bool_from_int((ND(g, m)->flags & AST_F_OPERATOR) != 0);
            const char *op_str = is_op ? gen_operator_name(g, m) : VM_NULL;
            add_req_method(g, tid, lsid, mt->start, mt->length, ND(g, m)->a, ND(g, m)->c,
                           (ND(g, m)->b != 0) ? m : 0, is_op, op_str);
            continue;
        }
        /* FIELD: either a data field, or `name: (params): R` method signature */
        AstRef fty = ND(g, m)->a;
        if (fty != 0 && ND(g, fty)->kind == AST_TY_FUNC) {
            add_req_method(g, tid, lsid, mt->start, mt->length, ND(g, fty)->a, ND(g, fty)->b, 0, VM_FALSE, VM_NULL);
            continue;
        }
        VMDataType ftype = gen_type(g, fty, VM_NULL, VM_NULL, VM_NULL);
        vm_bool has_def = VM_FALSE;
        VMReg def_val = {0};
        if (ND(g, m)->b != 0) {
            def_val = const_value(g, ND(g, m)->b, ftype, VM_NULL);
            has_def = VM_TRUE;
        }
        if (tc->num_req_fields < 16) {
            vm_u8 fidx = tc->num_req_fields++;
            vm_str_copy(tc->req_field_names[fidx], mt->start, mt->length, 32);
            tc->req_field_types[fidx] = ftype;
            tc->req_field_has_default[fidx] = has_def;
            tc->req_field_defaults[fidx] = def_val;
        }
        if (lsd->num_fields < MAX_STRUCT_FIELDS) {
            vm_u8 lfidx = lsd->num_fields++;
            vm_str_copy(lsd->field_names[lfidx], mt->start, mt->length, 32);
            lsd->field_types[lfidx] = ftype;
            lsd->field_is_pub[lfidx] = VM_TRUE;
            lsd->field_has_default[lfidx] = has_def;
            lsd->field_defaults[lfidx] = def_val;
        }
    }
    if (lsd->num_fields == 0) {
        lsd->num_fields = 1;
        lsd->field_names[0][0] = '\0';
        lsd->field_types[0] = VM_TYPE_I32;
        lsd->field_is_pub[0] = VM_FALSE;
        lsd->field_has_default[0] = VM_FALSE;
    }

    for (vm_u8 rm = 0; rm < tc->num_req_methods; rm++) {
        for (vm_u8 p = 0; p < tc->req_method_arity[rm] && p < 8; p++) {
            if (tc->req_method_param_is_self[rm][p] == VM_TRUE) {
                tc->req_method_param_types[rm][p] = lsd->field_types[0];
                tc->req_method_param_sids[rm][p] = lsid;
            }
        }
        if (tc->req_method_ret_is_self[rm] == VM_TRUE) {
            tc->req_method_ret[rm] = lsd->field_types[0];
            tc->req_method_ret_sid[rm] = lsid;
        }
        if (g->contract_body[tid][rm] != 0) {
            c->current_struct_id = lsid;
            (void)gen_function(g, g->contract_body[tid][rm], lsd->name, tc->req_method_names[rm], VM_TRUE);
        } else {
            vm_u32 fid = c_alloc_func_slot(c);
            VMFunctionInfo *fn = &c->chunk->functions[fid];
            vm_str_join_dot(fn->name, 64, lsd->name, vm_str_len(lsd->name), tc->req_method_names[rm], vm_str_len(tc->req_method_names[rm]));
            init_function(fn);
            fn->arity = tc->req_method_arity[rm];
            fn->min_arity = tc->req_method_arity[rm];
            fn->owner_struct_id = (vm_i16)lsid;
            for (vm_u8 p = 0; p < fn->arity && p < 8; p++) {
                fn->param_types[p] = tc->req_method_param_types[rm][p];
                fn->param_struct_ids[p] = (vm_i16)tc->req_method_param_sids[rm][p];
                fn->param_has_default[p] = VM_FALSE;
            }
            fn->return_type = tc->req_method_ret[rm];
            fn->return_struct_id = (vm_i16)tc->req_method_ret_sid[rm];
            fn->return_reg_count = (fn->return_struct_id >= 0) ? c->structs[fn->return_struct_id].num_fields : 1;
            fn->return_elem_types[0] = fn->return_type;
            fn->start_ip = 0xFFFFFFFFu;
            fn->num_regs = fn->arity;
        }
    }
    c->current_struct_id = saved_struct_id;
}

/* ---------------------------------------------------------------------------
 * struct
 * ------------------------------------------------------------------------- */

static void gen_struct(Gen *g, AstRef n) {
    Compiler *c = g->c;
    if (ND(g, n)->flags & AST_F_TYPE_HDR) {
        gen_contract(g, ND(g, n)->tok, ND(g, n)->c);
        return;
    }
    const Token *nt = gen_tok(g, ND(g, n)->tok);
    vm_bool is_exported = vm_bool_from_int((ND(g, n)->flags & AST_F_EXPORT) != 0);

    int sid = c->num_structs++;
    StructDef *sd = &c->structs[sid];
    vm_str_copy(sd->name, nt->start, nt->length, 64);
    sd->owner_type_id = -1;
    sd->parent_struct_id = -1;
    sd->num_parents = 0;
    sd->num_implemented_types = 0;
    sd->num_fields = 0;
    sd->is_generic = VM_FALSE;
    sd->is_optional = VM_FALSE;
    sd->is_enum = VM_FALSE;
    sd->is_result = VM_FALSE;
    sd->num_type_params = 0;

    AstList generics = ND(g, n)->a;
    if (LLEN(g, generics) > 0) {
        sd->is_generic = VM_TRUE;
        const Token *tp = gen_tok(g, LAT(g, generics, 0));
        vm_str_copy(sd->type_param_name, tp->start, tp->length, 32);
        sd->num_type_params = (vm_u8)LLEN(g, generics);
    }

    /* Re-opening a primitive struct (`struct String { ... }`) extends the existing one */
    for (int i = 0; i < sid; i++) {
        if (c->structs[i].owner_type_id >= 0) continue;
        vm_u32 slen = vm_str_len(c->structs[i].name);
        if (vm_str_eq(c->structs[i].name, slen, sd->name, nt->length) == VM_TRUE &&
            c->structs[i].num_type_params == sd->num_type_params) {
            vm_bool is_prim = VM_FALSE;
            for (int pt = 0; pt < VM_TYPE_COUNT; pt++) {
                const char *pname = c->chunk->primitive_struct_name[pt];
                if (pname[0] != '\0' && vm_str_eq(sd->name, nt->length, pname, vm_str_len(pname)) == VM_TRUE) {
                    is_prim = VM_TRUE;
                    break;
                }
            }
            if (is_prim == VM_TRUE || vm_str_eq_cstr(sd->name, nt->length, "String") == VM_TRUE) {
                c->num_structs--;
                sid = i;
                sd = &c->structs[sid];
                break;
            }
            gen_error(g, n, "Duplicate struct or generic struct with identical type parameter count");
            return;
        }
    }

    AstList parents = ND(g, n)->b;
    for (vm_u32 i = 0; i < LLEN(g, parents); i++) {
        AstRef pt = LAT(g, parents, i);
        const Token *ptok = gen_tok(g, ND(g, pt)->tok);
        int tid = c_resolve_type_contract(c, ptok->start, ptok->length);
        int pid = (tid < 0) ? c_resolve_struct(c, ptok->start, ptok->length) : -1;
        if (pid >= 0) {
            if (sd->parent_struct_id < 0) sd->parent_struct_id = pid;
            if (sd->num_parents < 4) sd->parent_struct_ids[sd->num_parents++] = pid;
            StructDef *parent = &c->structs[pid];
            for (vm_u8 f = 0; f < parent->num_fields; f++) {
                if (parent->field_names[f][0] == '\0' || sd->num_fields >= MAX_STRUCT_FIELDS) continue;
                vm_u8 idx = sd->num_fields++;
                vm_str_copy(sd->field_names[idx], parent->field_names[f], 31, 32);
                sd->field_types[idx] = parent->field_types[f];
                sd->field_is_pub[idx] = parent->field_is_pub[f];
                sd->field_has_default[idx] = parent->field_has_default[f];
                sd->field_defaults[idx] = parent->field_defaults[f];
            }
        } else if (tid >= 0) {
            if (sd->num_implemented_types < 4) {
                sd->implemented_types[sd->num_implemented_types++] = tid;
                c->type_contracts[tid].last_impl_struct_id = sid;
            }
            TypeContract *tc = &c->type_contracts[tid];
            for (vm_u8 rf = 0; rf < tc->num_req_fields; rf++) {
                vm_bool exists = VM_FALSE;
                vm_u32 rflen = vm_str_len(tc->req_field_names[rf]);
                for (vm_u8 sf = 0; sf < sd->num_fields; sf++) {
                    if (vm_str_eq(sd->field_names[sf], vm_str_len(sd->field_names[sf]), tc->req_field_names[rf], rflen) == VM_TRUE) {
                        exists = VM_TRUE;
                        break;
                    }
                }
                if (exists == VM_FALSE && sd->num_fields < MAX_STRUCT_FIELDS) {
                    vm_u8 idx = sd->num_fields++;
                    vm_str_copy(sd->field_names[idx], tc->req_field_names[rf], 31, 32);
                    sd->field_types[idx] = tc->req_field_types[rf];
                    sd->field_is_pub[idx] = VM_TRUE;
                    sd->field_has_default[idx] = tc->req_field_has_default[rf];
                    sd->field_defaults[idx] = tc->req_field_defaults[rf];
                }
            }
        } else {
            gen_error(g, pt, "Unknown parent struct or type contract");
            return;
        }
    }

    c->current_struct_id = sid;
    AstList members = ND(g, n)->c;

    /* Pass 1: fields (so methods can use fields declared after them) */
    for (vm_u32 i = 0; i < LLEN(g, members) && !c->had_error; i++) {
        AstRef m = LAT(g, members, i);
        if (ND(g, m)->kind != AST_FIELD) continue;
        const Token *ft = gen_tok(g, ND(g, m)->tok);
        VMDataType ftype = gen_type(g, ND(g, m)->a, VM_NULL, VM_NULL, VM_NULL);
        int existing = -1;
        for (vm_u8 sf = 0; sf < sd->num_fields; sf++) {
            if (vm_str_eq(sd->field_names[sf], vm_str_len(sd->field_names[sf]), ft->start, ft->length) == VM_TRUE) {
                existing = (int)sf;
                break;
            }
        }
        if (existing < 0 && sd->num_fields >= MAX_STRUCT_FIELDS) {
            gen_error(g, m, "Too many struct fields");
            break;
        }
        vm_u8 fidx = (existing >= 0) ? (vm_u8)existing : sd->num_fields++;
        if (existing < 0) {
            vm_str_copy(sd->field_names[fidx], ft->start, ft->length, 32);
            sd->field_has_default[fidx] = VM_FALSE;
            sd->field_defaults[fidx] = (VMReg){0};
        }
        sd->field_types[fidx] = ftype;
        sd->field_is_pub[fidx] = vm_bool_from_int((ND(g, m)->flags & (AST_F_PUB | AST_F_EXPORT)) != 0);
        if (ND(g, m)->b != 0) {
            sd->field_has_default[fidx] = VM_TRUE;
            sd->field_defaults[fidx] = const_value(g, ND(g, m)->b, ftype, VM_NULL);
        }
    }
    if (sd->num_fields == 0) {
        sd->num_fields = 1;
        sd->field_names[0][0] = '\0';
        sd->field_types[0] = VM_TYPE_I32;
        sd->field_is_pub[0] = VM_FALSE;
        sd->field_has_default[0] = VM_FALSE;
    }

    /* Pass 2: methods & operators */
    for (vm_u32 i = 0; i < LLEN(g, members) && !c->had_error; i++) {
        AstRef m = LAT(g, members, i);
        if (ND(g, m)->kind != AST_FUNC) continue;
        c->current_struct_id = sid;
        if (ND(g, m)->flags & AST_F_OPERATOR) {
            (void)gen_function(g, m, sd->name, gen_operator_name(g, m), VM_TRUE);
        } else {
            vm_bool member_pub = vm_bool_from_int((ND(g, m)->flags & (AST_F_PUB | AST_F_EXPORT)) != 0);
            (void)gen_function(g, m, sd->name, VM_NULL, member_pub);
        }
    }

    /* Inherit default method bodies from bound `type` contracts */
    for (vm_u8 t = 0; t < sd->num_implemented_types && !c->had_error; t++) {
        int tid = sd->implemented_types[t];
        TypeContract *tc = &c->type_contracts[tid];
        for (vm_u8 rf = 0; rf < tc->num_req_fields; rf++) {
            vm_bool found = VM_FALSE;
            vm_u32 rflen = vm_str_len(tc->req_field_names[rf]);
            for (vm_u8 sf = 0; sf < sd->num_fields; sf++) {
                if (vm_str_eq(sd->field_names[sf], vm_str_len(sd->field_names[sf]), tc->req_field_names[rf], rflen) == VM_TRUE &&
                    sd->field_types[sf] == tc->req_field_types[rf]) {
                    found = VM_TRUE;
                    break;
                }
            }
            if (found == VM_FALSE) {
                gen_error(g, n, "Struct is missing a required field (or has mismatched type) from bound 'type' contract");
                return;
            }
        }
        for (vm_u8 rm = 0; rm < tc->num_req_methods; rm++) {
            if (g->contract_body[tid][rm] == 0) continue;
            char mqname[64];
            vm_str_join_dot(mqname, 64, sd->name, vm_str_len(sd->name), tc->req_method_names[rm], vm_str_len(tc->req_method_names[rm]));
            vm_u32 mqlen = vm_str_len(mqname);
            vm_bool has_override = VM_FALSE;
            for (vm_u32 f = 0; f < c->chunk->num_functions; f++) {
                if (vm_str_eq(c->chunk->functions[f].name, vm_str_len(c->chunk->functions[f].name), mqname, mqlen) == VM_TRUE &&
                    c->chunk->functions[f].arity == tc->req_method_arity[rm]) {
                    has_override = VM_TRUE;
                    break;
                }
            }
            if (has_override == VM_FALSE) {
                AstTree *saved_t = g->t;
                g->t = g->contract_tree[tid];
                c->current_struct_id = sid;
                (void)gen_function(g, g->contract_body[tid][rm], sd->name, tc->req_method_names[rm], VM_TRUE);
                g->t = saved_t;
            }
        }
    }

    c->current_struct_id = -1;
    if (is_exported) {
        if (c->module_prefix[0] != '\0') {
            vm_u32 mplen = vm_str_len(c->module_prefix);
            c_export_struct_namespace(c, sd->name, vm_str_len(sd->name), c->module_prefix, mplen - 1);
        } else {
            c_export_struct_namespace(c, sd->name, vm_str_len(sd->name), nt->start, nt->length);
        }
    }
}

/* ---------------------------------------------------------------------------
 * enum / const
 * ------------------------------------------------------------------------- */

static void register_variant(Gen *g, const char *prefix, vm_u32 plen, const Token *vname, vm_i32 tag,
                             int enum_sid, vm_u8 payload_count, const VMDataType *payload_types) {
    Compiler *c = g->c;
    GlobalConst *gc = &c->global_consts[c->num_global_consts++];
    vm_str_join_dot(gc->name, 64, prefix, plen, vname->start, vname->length);
    gc->length = vm_str_len(gc->name);
    gc->type = VM_TYPE_I32;
    gc->val.i32 = tag;
    if (c->num_enum_variants < 128) {
        EnumVariant *ev = &c->enum_variants[c->num_enum_variants++];
        vm_str_copy(ev->qname, gc->name, gc->length, 64);
        ev->tag = tag;
        ev->has_payload = vm_bool_from_int(payload_count > 0);
        ev->is_tagged_enum = VM_TRUE;
        ev->payload_type = (payload_count > 0) ? payload_types[0] : VM_TYPE_I32;
        ev->enum_sid = enum_sid;
        ev->payload_count = payload_count;
        for (vm_u8 i = 0; i < payload_count && i < MAX_VARIANT_PAYLOAD; i++) ev->payload_types[i] = payload_types[i];
    }
}

/*
 * `enum Shape { Circle(r: float), Rect(float, float), Empty }`
 * Values are structs {tag: i32, p0, p1, ...} sized for the largest payload.
 */
static void gen_enum(Gen *g, AstRef n) {
    Compiler *c = g->c;
    const Token *en = gen_tok(g, ND(g, n)->tok);
    vm_u32 alias_tok = ND(g, n)->b;
    const Token *alias = (alias_tok != 0) ? gen_tok(g, alias_tok) : en;
    vm_bool export_alias = vm_bool_from_int(((ND(g, n)->flags & AST_F_EXPORT) != 0 || alias_tok != 0) &&
                                            vm_str_eq(en->start, en->length, alias->start, alias->length) == VM_FALSE);
    AstList variants = ND(g, n)->a;
    vm_u32 nv = LLEN(g, variants);

    if (c->num_structs >= 64) {
        gen_error(g, n, "Too many struct / enum types");
        return;
    }
    int sid = c->num_structs++;
    StructDef *sd = &c->structs[sid];
    vm_str_copy(sd->name, en->start, en->length, 64);
    sd->owner_type_id = -1;
    sd->parent_struct_id = -1;
    sd->num_parents = 0;
    sd->num_implemented_types = 0;
    sd->is_generic = VM_FALSE;
    sd->num_type_params = 0;
    sd->is_optional = VM_FALSE;
    sd->is_enum = VM_TRUE;
    sd->is_result = VM_FALSE;
    sd->num_fields = 1;
    vm_str_copy(sd->field_names[0], "tag", 3, 32);
    sd->field_types[0] = VM_TYPE_I32;
    sd->field_is_pub[0] = VM_TRUE;
    sd->field_has_default[0] = VM_FALSE;
    sd->field_defaults[0] = (VMReg){0};

    VMDataType ptypes[64][MAX_VARIANT_PAYLOAD];
    vm_u8 pcount[64];
    vm_i32 tags[64];
    vm_i32 val = 0;
    for (vm_u32 i = 0; i < nv && i < 64; i++) {
        AstRef v = LAT(g, variants, i);
        AstList payload = ND(g, v)->a;
        pcount[i] = 0;
        if (LLEN(g, payload) > MAX_VARIANT_PAYLOAD) {
            gen_error(g, v, "Enum variants can carry at most 4 values");
            return;
        }
        for (vm_u32 k = 0; k < LLEN(g, payload); k++) {
            int psid = -1;
            ptypes[i][k] = gen_type(g, LAT(g, payload, k), &psid, VM_NULL, VM_NULL);
            if (psid >= 0) {
                gen_error(g, v, "Enum payloads cannot be structs yet");
                return;
            }
            pcount[i]++;
            /* payload slot k: the first variant using it decides its declared type */
            if (sd->num_fields <= k + 1) {
                vm_u8 f = sd->num_fields++;
                char fname[8] = {'p', (char)('0' + k), 0};
                vm_str_copy(sd->field_names[f], fname, 2, 32);
                sd->field_types[f] = ptypes[i][k];
                sd->field_is_pub[f] = VM_TRUE;
                sd->field_has_default[f] = VM_FALSE;
                sd->field_defaults[f] = (VMReg){0};
            }
        }
        if (pcount[i] == 0 && ND(g, v)->b != 0 && ND(g, ND(g, v)->b)->kind == AST_INT) {
            val = (vm_i32)gen_tok(g, ND(g, ND(g, v)->b)->tok)->int_val;
        }
        tags[i] = val++;
    }

    sd->enum_first = c->num_enum_variants;
    sd->enum_count = (int)nv;
    for (vm_u32 i = 0; i < nv && i < 64; i++) {
        register_variant(g, en->start, en->length, gen_tok(g, ND(g, LAT(g, variants, i))->tok), tags[i], sid, pcount[i], ptypes[i]);
    }
    if (export_alias) {
        for (vm_u32 i = 0; i < nv && i < 64; i++) {
            register_variant(g, alias->start, alias->length, gen_tok(g, ND(g, LAT(g, variants, i))->tok), tags[i], sid, pcount[i], ptypes[i]);
        }
    }
}

static void add_global_const(Compiler *c, const char *prefix, vm_u32 plen, const Token *name, VMDataType type, VMReg val) {
    GlobalConst *gc = &c->global_consts[c->num_global_consts++];
    if (plen > 0) vm_str_join_dot(gc->name, 64, prefix, plen, name->start, name->length);
    else vm_str_copy(gc->name, name->start, name->length, 64);
    gc->length = vm_str_len(gc->name);
    gc->type = type;
    gc->val = val;
}

static void gen_const_decl(Gen *g, AstRef n) {
    Compiler *c = g->c;
    const Token *name = gen_tok(g, ND(g, n)->tok);
    vm_bool exported = vm_bool_from_int((ND(g, n)->flags & AST_F_EXPORT) != 0);
    AstRef value = ND(g, n)->b;
    vm_u32 ns_tok = ND(g, n)->c;

    /* `export const x = 0 as std.math` parses as a cast to `std.math`: treat an unknown target as the namespace */
    if (ns_tok == 0 && value != 0 && ND(g, value)->kind == AST_BINARY && ND(g, value)->op == TOK_AS) {
        AstRef target = ND(g, value)->b;
        if (ND(g, target)->kind == AST_TY_NAME) {
            const Token *tt = gen_tok(g, ND(g, target)->tok);
            if (tt->type == TOK_IDENT && c_resolve_struct(c, tt->start, tt->length) < 0 &&
                c_resolve_type_contract(c, tt->start, tt->length) < 0) {
                ns_tok = ND(g, target)->tok;
                value = ND(g, value)->a;
            }
        }
    }

    VMDataType type = GEN_ANY;
    vm_bool has_type = vm_bool_from_int(ND(g, n)->a != 0);
    if (has_type) type = gen_type(g, ND(g, n)->a, VM_NULL, VM_NULL, VM_NULL);
    VMDataType vtype = type;
    VMReg val = const_value(g, value, type, &vtype);
    if (!has_type) type = vtype;

    if (ns_tok != 0) {
        const Token *ns = gen_tok(g, ns_tok);
        add_global_const(c, ns->start, ns->length, name, type, val);
    } else {
        add_global_const(c, c->module_prefix, vm_str_len(c->module_prefix) > 0 ? vm_str_len(c->module_prefix) - 1 : 0, name, type, val);
    }
    if (exported) {
        add_global_const(c, "", 0, name, type, val);
        if (c->module_prefix[0] != '\0') add_global_const(c, c->module_prefix, vm_str_len(c->module_prefix) - 1, name, type, val);
    }
}

/* ---------------------------------------------------------------------------
 * import / export
 * ------------------------------------------------------------------------- */

static void gen_import(Gen *g, AstRef n) {
    Compiler *c = g->c;
    AstList path = ND(g, n)->a;
    Token pkg = gen_token(g, LAT(g, path, 0));
    Token mod = (LLEN(g, path) > 1) ? gen_token(g, LAT(g, path, LLEN(g, path) - 1)) : pkg;
    Token last = mod;   /* the module's own name in code (handle) */
    /* `import a.b.c.d`: file a's root / b/c/d.vml — the segments after the root become a path */
    char subpath[128];
    if (LLEN(g, path) > 2 && !(ND(g, n)->flags & AST_F_PATH)) {
        vm_u32 k = 0;
        for (vm_u32 i = 1; i < LLEN(g, path); i++) {
            Token seg = gen_token(g, LAT(g, path, i));
            if (i > 1 && k < 126) subpath[k++] = '/';
            for (vm_u32 j = 0; j < seg.length && k < 126; j++) subpath[k++] = seg.start[j];
        }
        subpath[k] = '\0';
        mod.start = subpath;
        mod.length = k;
    }
    gen_at(g, n);
    vm_bool has_braces = vm_bool_from_int((ND(g, n)->flags & AST_F_BRACES) != 0);
    vm_bool has_alias = vm_bool_from_int(ND(g, n)->c != 0);
    Token alias = has_alias ? gen_token(g, ND(g, n)->c) : pkg;
    int midx;
    if (ND(g, n)->flags & AST_F_PATH) {
        /* `import "./x"`: an internal file joins this module; a file naming another module loads it */
        vm_bool internal = VM_FALSE;
        midx = c_import_path(c, pkg.start, pkg.length, has_braces,
                             has_alias ? alias.start : VM_NULL, has_alias ? alias.length : 0, &internal);
        if (internal && (ND(g, n)->flags & AST_F_PUB) && !c->had_error) {
            gen_error(g, n, "'pub import' of a file that is part of this module: mark what it shares with 'pub' in that file instead");
            return;
        }
        if (c->had_error || internal || midx < 0) return;
    } else {
        /* The module's own name in code: `as` name when it is first loaded with one, else the last segment */
        Token handle = (has_alias && !has_braces) ? alias : last;
        midx = c_load_module_file(c, pkg.start, pkg.length, mod.start, mod.length, handle.start, handle.length);
        if (c->had_error || midx < 0) return;
    }
    /* This module may now name the imported one, plus everything that one shares with `pub import` */
    {
        int me = c->cur_module_idx + 1;
        vm_u32 got = (1u << midx) | c->mod_pubexp[midx + 1];
        c->mod_visible[me] |= got;
        if (ND(g, n)->flags & AST_F_PUB) c->mod_pubexp[me] |= got;
    }
    /* Symbols are found under the handle the module was first loaded with */
    mod.start = c->loaded_modules[midx];
    mod.length = vm_str_len(c->loaded_modules[midx]);

    if (has_braces) {
        AstList items = ND(g, n)->b;
        Token sel[16];
        int num_sel = 0;
        vm_bool wildcard = VM_FALSE;
        const char *prefix = has_alias ? alias.start : "";
        vm_u32 plen = has_alias ? alias.length : 0;
        for (vm_u32 i = 0; i < LLEN(g, items); i++) {
            AstRef it = LAT(g, items, i);
            Token orig = gen_token(g, ND(g, it)->tok);
            if (orig.type == TOK_STAR) {
                wildcard = VM_TRUE;
                continue;
            }
            Token bound = (ND(g, it)->a != 0) ? gen_token(g, ND(g, it)->a) : orig;
            if (num_sel < 16) sel[num_sel++] = orig;
            char target_name[64], bound_name[64];
            vm_str_join_dot(target_name, 64, mod.start, mod.length, orig.start, orig.length);
            if (plen > 0) vm_str_join_dot(bound_name, 64, prefix, plen, bound.start, bound.length);
            else vm_str_copy(bound_name, bound.start, bound.length, 64);
            int cid = c_resolve_global_const(c, target_name, vm_str_len(target_name));
            if (cid >= 0) {
                GlobalConst *gc = &c->global_consts[c->num_global_consts++];
                vm_str_copy(gc->name, bound_name, vm_str_len(bound_name), 64);
                gc->length = vm_str_len(gc->name);
                gc->type = c->global_consts[cid].type;
                gc->val = c->global_consts[cid].val;
            } else if (c_resolve_func(c, target_name, vm_str_len(target_name)) >= 0) {
                c_bind_all_overloads(c, target_name, bound_name);
            } else {
                gen_error(g, it, "Imported symbol not found in module");
                return;
            }
        }
        if (wildcard) c_bind_wildcard_symbols(c, mod, prefix, plen, sel, num_sel);
    } else if (has_alias && vm_str_eq(alias.start, alias.length, mod.start, mod.length) == VM_FALSE) {
        c_bind_wildcard_symbols(c, mod, alias.start, alias.length, VM_NULL, 0);
    }
}

/* Builds "a" + b + "c"... into out (capped) */
static void join3(char *out, vm_u32 cap, const char *a, const char *b, const char *c2, const char *d, const char *e) {
    const char *parts[5] = {a, b, c2, d, e};
    vm_u32 n = 0;
    for (int k = 0; k < 5; k++) {
        if (parts[k] == VM_NULL) continue;
        vm_u32 pl = vm_str_len(parts[k]);
        for (vm_u32 j = 0; j < pl && n + 1 < cap; j++) out[n++] = parts[k][j];
    }
    out[n] = '\0';
}

/* Length of the first segment of a dotted name */
static vm_u32 root_len(const char *name) {
    vm_u32 n = 0;
    while (name[n] != '\0' && name[n] != '.') n++;
    return n;
}

/*
 * `module a.b`: names the file's module. The root (`a`) must be the package it was loaded from,
 * and `std` belongs to the files built into the binary, so a package cannot pose as another module.
 */
static void gen_module_decl(Gen *g, AstRef n) {
    Compiler *c = g->c;
    AstList path = ND(g, n)->a;
    char declared[128];
    declared[0] = '\0';
    for (vm_u32 i = 0; i < LLEN(g, path); i++) {
        Token t = gen_token(g, LAT(g, path, i));
        vm_u32 dl = vm_str_len(declared);
        if (i == 0) vm_str_copy(declared, t.start, t.length, 128);
        else if (dl + 1 + t.length < 128) {
            declared[dl] = '.';
            vm_str_copy(declared + dl + 1, t.start, t.length, 128 - dl - 1);
        }
    }
    char msg[200];
    vm_u32 rl = root_len(declared);
    if (vm_str_eq_cstr(declared, rl, "std") == VM_TRUE && c->in_std_module == VM_FALSE) {
        join3(msg, sizeof(msg), "Module name '", declared, "' is reserved for the standard library", VM_NULL, VM_NULL);
        gen_error(g, n, msg);
        return;
    }
    if (c->cur_module_idx < 0) return;   /* the main program may name itself */
    char *full = c->loaded_full[c->cur_module_idx];
    vm_u32 frl = root_len(full);
    if (vm_str_eq(declared, rl, full, frl) == VM_FALSE) {
        char root[64];
        vm_str_copy(root, full, frl, 64);
        join3(msg, sizeof(msg), "Module name '", declared, "' must be under '", root, "', the package it was loaded from");
        gen_error(g, n, msg);
        return;
    }
    vm_str_copy(full, declared, vm_str_len(declared), 128);
}

static void gen_export(Gen *g, AstRef n) {
    AstList items = ND(g, n)->a;
    for (vm_u32 i = 0; i < LLEN(g, items); i++) {
        AstRef it = LAT(g, items, i);
        Token orig = gen_token(g, ND(g, it)->tok);
        vm_bool has_as = vm_bool_from_int(ND(g, it)->a != 0);
        Token exported = has_as ? gen_token(g, ND(g, it)->a) : orig;
        gen_at(g, it);
        c_export_single_symbol(g->c, orig, exported, has_as);
    }
}

/* ---------------------------------------------------------------------------
 * Units & entry point
 * ------------------------------------------------------------------------- */

/* Plain top-level functions are declared before any body is compiled, so they can call each other in any order */
static vm_bool forward_declarable(Gen *g, AstRef it) {
    const AstNode *nd = ND(g, it);
    if (nd->kind != AST_FUNC || (nd->flags & (AST_F_PROTO | AST_F_OPERATOR | AST_F_LAMBDA))) return VM_FALSE;
    if (LLEN(g, AST_FUNC_GENERICS(g->t, it)) > 0 || LLEN(g, AST_FUNC_ATTRS(g->t, it)) > 0) return VM_FALSE;
    return vm_bool_from_int(nd->b != 0);
}

void gen_unit(Gen *g, AstRef unit) {
    Compiler *c = g->c;
    AstList items = ND(g, unit)->a;
    /* Pass 1: types and declarations in order; plain functions as signatures only */
    for (vm_u32 i = 0; i < LLEN(g, items) && !c->had_error; i++) {
        AstRef it = LAT(g, items, i);
        gen_at(g, it);
        if (forward_declarable(g, it)) {
            g->proto_pass = VM_TRUE;
            (void)gen_function(g, it, c->module_prefix, VM_NULL, VM_TRUE);
            g->proto_pass = VM_FALSE;
            continue;
        }
        vm_u16 flags = ND(g, it)->flags;
        switch ((AstKind)ND(g, it)->kind) {
            case AST_IMPORT:     gen_import(g, it); break;
            case AST_MODULE:     gen_module_decl(g, it); break;
            case AST_EXPORT:     gen_export(g, it); break;
            case AST_ENUM:       gen_enum(g, it); break;
            case AST_STRUCT:     gen_struct(g, it); break;
            case AST_CONTRACT:   gen_contract(g, ND(g, it)->tok, ND(g, it)->c); break;
            case AST_CONST:      gen_const_decl(g, it); break;
            case AST_TYPE_ALIAS: {
                AstRef ty = ND(g, it)->a;
                if (ty != 0 && ND(g, ty)->kind == AST_TY_FUNC) {
                    gen_func_prototype_type(g, ND(g, it)->tok, ND(g, ty)->a, ND(g, ty)->b);
                } else {
                    const Token *nt = gen_tok(g, ND(g, it)->tok);
                    int tid = new_contract(g, nt->start, nt->length);
                    c->type_contracts[tid].is_alias = VM_TRUE;
                    c->type_contracts[tid].alias_type = gen_type(g, ty, &c->type_contracts[tid].alias_struct_id, VM_NULL, VM_NULL);
                }
                break;
            }
            case AST_FUNC:
                if ((flags & AST_F_PROTO) && !(flags & AST_F_OPERATOR) && LLEN(g, AST_FUNC_ATTRS(g->t, it)) == 0) {
                    /* `type func name(...)`: callable prototype type + forward slot */
                    gen_func_prototype_type(g, ND(g, it)->tok, ND(g, it)->a, ND(g, it)->c);
                } else if (flags & AST_F_OPERATOR) {
                    (void)gen_function(g, it, VM_NULL, gen_operator_name(g, it), VM_TRUE);
                } else {
                    (void)gen_function(g, it, c->module_prefix, VM_NULL, VM_TRUE);
                }
                break;
            default:
                gen_error(g, it, "Unexpected top-level declaration");
                break;
        }
    }
    /* Pass 2: bodies of the forward-declared functions (they reuse their declared slots) */
    for (vm_u32 i = 0; i < LLEN(g, items) && !c->had_error; i++) {
        AstRef it = LAT(g, items, i);
        if (!forward_declarable(g, it)) continue;
        gen_at(g, it);
        (void)gen_function(g, it, c->module_prefix, VM_NULL, VM_TRUE);
    }
}

static AstTree *new_tree(Gen *g) {
    if (g->num_trees >= GEN_MAX_TREES) return VM_NULL;
    AstTree *t = (AstTree *)g->c->chunk->realloc_fn(g->c->chunk->alloc_ud, VM_NULL, 0, sizeof(AstTree));
    if (t == VM_NULL) return VM_NULL;
    ast_init(t, g->c->chunk->realloc_fn, g->c->chunk->alloc_ud);
    g->trees[g->num_trees++] = t;
    return t;
}

/* Compiles an imported `.vml` module source (called back from the shared module loader) */
static void gen_compile_module(void *gen, const char *src, const char *path) {
    Gen *g = (Gen *)gen;
    AstTree *t = new_tree(g);
    if (t == VM_NULL) {
        c_error(g->c, "Out of memory while compiling module");
        return;
    }
    if (ast_parse(t, src) == VM_FALSE) {
        ast_print_errors(t, path);
        g->c->had_error = VM_TRUE;
        return;
    }
    AstTree *saved = g->t;
    g->t = t;
    gen_unit(g, t->root);
    g->t = saved;
}

vm_bool gen_compile_source(const char *source, VMChunk *chunk, const VMConfig *cfg) {
    Compiler *c = (Compiler *)chunk->realloc_fn(chunk->alloc_ud, VM_NULL, 0, sizeof(Compiler));
    Gen *g = (Gen *)chunk->realloc_fn(chunk->alloc_ud, VM_NULL, 0, sizeof(Gen));
    if (c == VM_NULL || g == VM_NULL) return VM_FALSE;

    c->chunk = chunk; c->config = cfg;
    c->num_locals = 0; c->num_outer_locals = 0; c->scope_depth = 0; c->next_reg = 0; c->max_reg = 0;
    c->num_global_consts = 0; c->num_func_aliases = 0; c->num_loaded_modules = 0; c->cur_module_idx = -1;
    for (int fi = 0; fi < VM_MAX_FUNCTIONS; fi++) c->func_module[fi] = -1;
    c_set_main_file(c, chunk->file_names[chunk->current_file_id]);
    for (int vi = 0; vi < 33; vi++) { c->mod_visible[vi] = 0; c->mod_pubexp[vi] = 0; }
    c->prelude_mask = 0;
    c_func_index_reset(c);
    c->num_structs = 0; c->current_struct_id = -1; c->num_type_contracts = 0;
    c->num_enum_variants = 0; c->num_generics = 0; c->num_active_tparams = 0;
    c->module_prefix[0] = '\0'; c->num_breaks = 0; c->num_continues = 0;
    c->num_defers = 0;
    c->pending_deprecated = VM_FALSE;
    c->pending_deprecated_msg[0] = '\0';
    c->pending_intrinsic[0] = '\0';
    c->in_std_module = VM_FALSE;
    c->pending_host_export[0] = '\0';
    c->current_ret_type = VM_TYPE_VOID;
    c->current_ret_struct_id = -1; c->current_func_id = -1; c->had_error = VM_FALSE;
    c->last_parsed_elem_type = VM_TYPE_VOID; c->last_empty_array_ip = 0xFFFFFFFFu;
    c->cur.type = TOK_EOF; c->cur.start = ""; c->cur.length = 0; c->cur.line = 1;
    c->ast_gen = g;
    c->ast_compile_module = gen_compile_module;

    g->c = c;
    g->t = VM_NULL;
    g->num_defers = 0;
    g->num_trees = 0;
    g->yield_active = VM_FALSE;
    g->yield_dst = 0;
    g->yield_type = VM_TYPE_VOID;
    g->fn_depth = 0;
    g->block_ftid = -1;
    g->proto_pass = VM_FALSE;
    g->num_instances = 0;
    g->num_decls = 0;
    for (vm_u32 i = 0; i < VM_MAX_FUNCTIONS; i++) {
        g->ret_elem[i] = VM_TYPE_VOID;
        g->inline_intr[i] = VM_NULL;
    }

    /* Sync from 0: host modules registered by an embedder before compiling are included */
    vm_stdlib_register_prelude_core(chunk);
    c_sync_native_stdlib(c, 0, 0, 0);

    vm_bool ok = VM_FALSE;
    AstTree *t = new_tree(g);
    if (t != VM_NULL) {
        if (ast_parse(t, source) == VM_FALSE) {
            ast_print_errors(t, chunk->file_names[chunk->current_file_id]);
        } else {
            g->t = t;
            /* Prelude written in .vml: methods of strings and arrays */
            const char *prelude[3] = {"array", "string", "map"};
            for (int pi = 0; pi < 3 && c->had_error == VM_FALSE; pi++) {
                vm_u32 pl = vm_str_len(prelude[pi]);
                int pidx = c_load_module_file(c, "std", 3, prelude[pi], pl, prelude[pi], pl);
                if (pidx >= 0) c->prelude_mask |= 1u << pidx;
            }
            g->t = t;
            if (c->had_error == VM_FALSE) gen_unit(g, t->root);
            if (c->had_error == VM_FALSE) check_declarations_defined(g);
            if (c->had_error == VM_FALSE) gen_build_interface(g, t, chunk->file_names[chunk->current_file_id]);
            if (c->had_error == VM_FALSE) c_verify_all_type_contracts(c);
            if (c->had_error == VM_FALSE) {
                if (chunk->entry_func_id < 0 && chunk->require_entry) {
                    vm_sys_write_cstr("[Compile Error] Missing entry point: please define 'func main()'\n");
                } else {
                    vm_fuse_superinstructions(chunk, 0, chunk->code_count);
                    ok = VM_TRUE;
                }
            }
        }
    }

    /* Trees must outlive compilation only for names copied into the chunk; locals are gone now.
     * String constants point into source buffers / arena, not into trees, so trees can go. */
    for (int i = 0; i < g->num_trees; i++) {
        ast_free(g->trees[i]);
        chunk->realloc_fn(chunk->alloc_ud, g->trees[i], sizeof(AstTree), 0);
    }
    chunk->realloc_fn(chunk->alloc_ud, g, sizeof(Gen), 0);
    chunk->realloc_fn(chunk->alloc_ud, c, sizeof(Compiler), 0);
    return ok;
}
