#include "decl.h"
#include "../stmt/stmt.h"
#include "../expr/expr.h"

void c_declare_param(Compiler *c, VMFunctionInfo *fn, Token pname, VMDataType ptype, int psid) {
    vm_u8 idx = fn->arity++;
    fn->param_struct_ids[idx] = (vm_i16)psid;
    fn->param_types[idx] = ptype;
    fn->param_has_default[idx] = VM_FALSE;
    fn->param_defaults[idx] = (VMReg){0};
    if (psid >= 0) {
        StructDef *sd = &c->structs[psid];
        c_declare_local(c, pname, sd->field_types[0], VM_FALSE, (VMReg){0});
        LocalVar *lv = &c->locals[c->num_locals - 1];
        lv->struct_id = psid;
        lv->elem_count = sd->num_fields;
        for (vm_u8 f = 0; f < sd->num_fields; f++) {
            lv->elem_types[f] = sd->field_types[f];
            if (f > 0) c_alloc_reg(c);
        }
    } else {
        c_declare_local(c, pname, ptype, VM_FALSE, (VMReg){0});
    }
}

static vm_bool is_inline_func_type(Compiler *c) {
    if (c_check(c, TOK_LPAREN) == VM_FALSE) return VM_FALSE;
    Lexer copy = c->lex;
    int depth = 1;
    while (depth > 0) {
        Token t = lexer_next(&copy);
        if (t.type == TOK_EOF) return VM_FALSE;
        if (t.type == TOK_LPAREN) depth++;
        else if (t.type == TOK_RPAREN) depth--;
    }
    Token after = lexer_next(&copy);
    return vm_bool_from_int(after.type == TOK_COLON);
}

static int parse_inline_func_type(Compiler *c) {
    c_consume(c, TOK_LPAREN, "Expected '(' in function type");
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
    if (c_check(c, TOK_RPAREN) == VM_FALSE) {
        do {
            if (c_check(c, TOK_IDENT) == VM_TRUE && lexer_peek(&c->lex).type == TOK_COLON) {
                c_advance(c);
                c_advance(c);
            }
            int psid = -1;
            VMDataType pt = c_parse_type_ex(c, &psid);
            if (tc->func_arity < 8) {
                tc->func_param_types[tc->func_arity] = pt;
                tc->func_param_sids[tc->func_arity] = psid;
                tc->func_arity++;
            }
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
    }
    c_consume(c, TOK_RPAREN, "Expected ')' in function type");
    c_consume(c, TOK_COLON, "Expected ':' before return type of function type");
    int rsid = -1;
    tc->func_ret_type = c_parse_type_ex(c, &rsid);
    tc->func_ret_sid = rsid;
    return ftid;
}

static vm_u8 peek_param_count(Compiler *c) {
    if (c_check(c, TOK_LPAREN) == VM_FALSE) return 0;
    Lexer lcopy = c->lex;
    Token t = lexer_next(&lcopy);
    if (t.type == TOK_RPAREN) return 0;
    vm_u8 cnt = 1;
    int depth = 0;
    while (t.type != TOK_EOF) {
        if (t.type == TOK_LPAREN || t.type == TOK_LT) depth++;
        else if (t.type == TOK_RPAREN) {
            if (depth == 0) break;
            depth--;
        } else if (t.type == TOK_GT && depth > 0) depth--;
        else if (t.type == TOK_COMMA && depth == 0) cnt++;
        t = lexer_next(&lcopy);
    }
    return cnt;
}

void c_compile_func_or_method(Compiler *c, const char *prefix, const char *custom_name, vm_bool is_pub) {
    int saved_struct_id = c->current_struct_id;
    Token name = {TOK_IDENT, "", 0, c->cur.line, 0, 0.0};
    if (custom_name == VM_NULL) {
        name = c->cur;
        if (c->cur.type == TOK_IDENT || c->cur.type == TOK_LEN ||
            (c->cur.type >= TOK_TYPE_I32 && c->cur.type <= TOK_TYPE_VOID)) {
            c_advance(c);
        } else {
            c_consume(c, TOK_IDENT, "Expected function name");
        }
        /* Out-of-line method implementation: `func Vec2.scale(...) { ... }` */
        if (c_match(c, TOK_DOT) == VM_TRUE) {
            int out_sid = c_resolve_struct(c, name.start, name.length);
            if (out_sid < 0) {
                c_error(c, "Unknown struct in out-of-line method declaration");
            } else {
                c->current_struct_id = out_sid;
                prefix = c->structs[out_sid].name;
            }
            name = c->cur;
            if (c->cur.type == TOK_IDENT || c->cur.type == TOK_LEN ||
                (c->cur.type >= TOK_TYPE_I32 && c->cur.type <= TOK_TYPE_VOID)) {
                c_advance(c);
            } else {
                c_consume(c, TOK_IDENT, "Expected method name after '.'");
            }
        }
    }

    if (custom_name == VM_NULL && c_match(c, TOK_LT) == VM_TRUE) {
        GenericTemplate *gt = &c->generics[c->num_generics++]; c->generics[c->num_generics - 1].module_idx = c->cur_module_idx;
        vm_str_copy(gt->name, name.start, name.length, 64);
        gt->num_type_params = 0;
        gt->is_variadic = VM_FALSE;
        do {
            Token tp = c->cur;
            c_consume(c, TOK_IDENT, "Expected generic parameter");
            vm_str_copy(gt->type_param_names[gt->num_type_params++], tp.start, tp.length, 32);
            if (c_match(c, TOK_ELLIPSIS) == VM_TRUE) gt->is_variadic = VM_TRUE;
            if (c_match(c, TOK_COLON) == VM_TRUE) (void)c_parse_type(c);
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
        c_consume(c, TOK_GT, "Expected '>'");
        gt->body_src_start = c->cur.start;
        while (c_check(c, TOK_LBRACE) == VM_FALSE && c_check(c, TOK_EOF) == VM_FALSE) c_advance(c);
        c_skip_block_tokens(c);
        c->current_struct_id = saved_struct_id;
        return;
    }

    vm_u32 fid = c_alloc_func_slot(c);
    VMFunctionInfo *fn = &c->chunk->functions[fid];

    const char *raw_name = (custom_name != VM_NULL) ? custom_name : name.start;
    vm_u32 raw_len = (custom_name != VM_NULL) ? vm_str_len(custom_name) : name.length;

    if (prefix != VM_NULL && prefix[0] != '\0') {
        vm_str_join_dot(fn->name, 64, prefix, vm_str_len(prefix), raw_name, raw_len);
    } else {
        vm_str_copy(fn->name, raw_name, raw_len, 64);
    }

    fn->arity = 0;
    fn->min_arity = 0;
    fn->is_pub = is_pub;
    fn->is_deprecated = c->pending_deprecated;
    fn->coerce_args_to_string = VM_FALSE;
    if (c->pending_deprecated == VM_TRUE) {
        vm_str_copy(fn->deprecated_msg, c->pending_deprecated_msg, vm_str_len(c->pending_deprecated_msg), 64);
    } else {
        fn->deprecated_msg[0] = '\0';
    }
    if (c->pending_host_export[0] != '\0') {
        c_register_alias(c, fn->name, (vm_i32)fid);
        vm_str_copy(fn->name, c->pending_host_export, vm_str_len(c->pending_host_export), 64);
        c_func_index_rename(c, fid);
    }
    c->pending_deprecated = VM_FALSE;
    c->pending_deprecated_msg[0] = '\0';
    c->pending_host_export[0] = '\0';

    fn->owner_struct_id = (vm_i16)c->current_struct_id;
    fn->return_type = VM_TYPE_VOID;
    fn->return_struct_id = -1;
    fn->return_func_type_id = -1;
    fn->return_reg_count = 1;
    fn->env_map_id = -1;
    fn->enclosing_func_id = -1;

    c->num_locals = 0; c->scope_depth = 1; c->next_reg = 0; c->max_reg = 0;

    /* Look up forward prototype or bound `type` contract signature for automatic type inference */
    vm_u8 peek_arity = peek_param_count(c);
    int proto_fid = -1;
    vm_u32 fn_name_len = vm_str_len(fn->name);
    for (int i = c_func_first_named(c, fn->name, fn_name_len); i >= 0 && i < (int)fid;
         i = c_func_next_named(c, i, fn->name, fn_name_len)) {
        VMFunctionInfo *ef = &c->chunk->functions[i];
        if (ef->start_ip == 0xFFFFFFFFu && ef->arity == peek_arity &&
            vm_str_eq(ef->name, vm_str_len(ef->name), fn->name, fn_name_len) == VM_TRUE) {
            proto_fid = i;
            break;
        }
    }

    TypeContract *proto_tc = VM_NULL;
    vm_u8 proto_rm = 0;
    if (c->current_struct_id >= 0) {
        StructDef *csd = &c->structs[c->current_struct_id];
        for (vm_u8 t = 0; t < csd->num_implemented_types && proto_tc == VM_NULL; t++) {
            TypeContract *tc = &c->type_contracts[csd->implemented_types[t]];
            for (vm_u8 rm = 0; rm < tc->num_req_methods; rm++) {
                vm_u32 rmlen = vm_str_len(tc->req_method_names[rm]);
                if (vm_str_eq(tc->req_method_names[rm], rmlen, raw_name, raw_len) == VM_TRUE &&
                    tc->req_method_arity[rm] == peek_arity) {
                    proto_tc = tc;
                    proto_rm = rm;
                    break;
                }
            }
        }
    }

    c_consume(c, TOK_LPAREN, "Expected '('");
    if (c_check(c, TOK_RPAREN) == VM_FALSE) {
        do {
            Token pname = c->cur;
            c_consume(c, TOK_IDENT, "Expected parameter name");
            vm_u8 pidx = fn->arity;
            int psid = -1;
            int pfunc_tid = -1;
            VMDataType ptype = VM_TYPE_I32;
            VMDataType pelem = VM_TYPE_VOID;

            if (c_match(c, TOK_COLON) == VM_TRUE) {
                if (is_inline_func_type(c) == VM_TRUE) {
                    pfunc_tid = parse_inline_func_type(c);
                    ptype = VM_TYPE_I32;
                } else {
                    if (c_check(c, TOK_IDENT) == VM_TRUE) {
                        int ftid = c_resolve_type_contract(c, c->cur.start, c->cur.length);
                        if (ftid >= 0 && c->type_contracts[ftid].is_func_type == VM_TRUE) {
                            pfunc_tid = ftid;
                        }
                    }
                    ptype = c_parse_type_ex(c, &psid);
                    pelem = c->last_parsed_elem_type;
                }
            } else if (proto_fid >= 0 && pidx < c->chunk->functions[proto_fid].arity) {
                ptype = c->chunk->functions[proto_fid].param_types[pidx];
                psid = c->chunk->functions[proto_fid].param_struct_ids[pidx];
            } else if (proto_tc != VM_NULL && pidx < proto_tc->req_method_arity[proto_rm]) {
                ptype = proto_tc->req_method_param_types[proto_rm][pidx];
                psid = (proto_tc->req_method_param_is_self[proto_rm][pidx] == VM_TRUE)
                       ? c->current_struct_id
                       : proto_tc->req_method_param_sids[proto_rm][pidx];
                if (psid >= 0 && c->structs[psid].num_fields > 0) {
                    ptype = c->structs[psid].field_types[0];
                }
            } else if (pidx == 0 && c->current_struct_id >= 0 &&
                       vm_str_eq_cstr(pname.start, pname.length, "self") == VM_TRUE) {
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
                c_consume(c, TOK_COLON, "Expected ':'");
            }

            c_declare_param(c, fn, pname, ptype, psid);
            if (ptype == VM_TYPE_ARRAY && psid < 0 && pelem != VM_TYPE_VOID) {
                LocalVar *plv = &c->locals[c->num_locals - 1];
                if (plv->elem_count == 0) plv->elem_count = 1;
                plv->elem_types[0] = pelem;
            }
            if (pfunc_tid >= 0) {
                c->locals[c->num_locals - 1].func_type_id = pfunc_tid;
            }

            /* Default parameter value: `func test(a: float = 1.0)` */
            if (c_match(c, TOK_ASSIGN) == VM_TRUE) {
                vm_u32 saved_cnt = c->chunk->code_count;
                vm_u8 saved_r = c->next_reg;
                ExprResult def_val = c_compile_expr(c, ptype);
                c->chunk->code_count = saved_cnt;
                c->next_reg = saved_r;
                fn->param_has_default[pidx] = VM_TRUE;
                fn->param_defaults[pidx] = def_val.const_val;
            } else {
                fn->min_arity++;
            }
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
    }
    c_consume(c, TOK_RPAREN, "Expected ')'");

    /* Refine `proto_fid` by exact parameter types in case of overloaded prototypes */
    for (int i = c_func_first_named(c, fn->name, fn_name_len); i >= 0 && i < (int)fid;
         i = c_func_next_named(c, i, fn->name, fn_name_len)) {
        VMFunctionInfo *ef = &c->chunk->functions[i];
        if (ef->start_ip != 0xFFFFFFFFu || ef->arity != fn->arity) continue;
        if (vm_str_eq(ef->name, vm_str_len(ef->name), fn->name, fn_name_len) == VM_FALSE) continue;
        vm_bool exact = VM_TRUE;
        for (vm_u8 p = 0; p < fn->arity; p++) {
            if (ef->param_types[p] != fn->param_types[p] ||
                ef->param_struct_ids[p] != fn->param_struct_ids[p]) {
                exact = VM_FALSE;
                break;
            }
        }
        if (exact == VM_TRUE) {
            proto_fid = i;
            break;
        }
    }

    if (c_match(c, TOK_COLON) == VM_TRUE) {
        if (is_inline_func_type(c) == VM_TRUE) {
            int ftid = parse_inline_func_type(c);
            fn->return_func_type_id = (vm_i16)ftid;
            fn->return_type = VM_TYPE_I32;
            fn->return_elem_types[0] = VM_TYPE_I32;
            fn->return_reg_count = 1;
        } else if (c_match(c, TOK_LPAREN) == VM_TRUE) {
            vm_u8 rcnt = 0;
            do {
                fn->return_elem_types[rcnt++] = c_parse_type(c);
            } while (c_match(c, TOK_COMMA) == VM_TRUE);
            c_consume(c, TOK_RPAREN, "Expected ')' after tuple return type");
            fn->return_type = fn->return_elem_types[0];
            fn->return_reg_count = rcnt;
        } else {
            if (c_check(c, TOK_IDENT) == VM_TRUE) {
                int ftid = c_resolve_type_contract(c, c->cur.start, c->cur.length);
                if (ftid >= 0 && c->type_contracts[ftid].is_func_type == VM_TRUE) {
                    fn->return_func_type_id = (vm_i16)ftid;
                }
            }
            int rsid = -1;
            fn->return_type = c_parse_type_ex(c, &rsid);
            fn->return_elem_types[0] = fn->return_type;
            fn->return_struct_id = (vm_i16)rsid;
            if (rsid >= 0) fn->return_reg_count = c->structs[rsid].num_fields;
        }
    } else if (proto_fid >= 0) {
        VMFunctionInfo *pf = &c->chunk->functions[proto_fid];
        fn->return_type = pf->return_type;
        fn->return_struct_id = pf->return_struct_id;
        fn->return_func_type_id = pf->return_func_type_id;
        fn->return_reg_count = pf->return_reg_count;
        for (vm_u8 r = 0; r < pf->return_reg_count; r++) {
            fn->return_elem_types[r] = pf->return_elem_types[r];
        }
    } else if (proto_tc != VM_NULL) {
        fn->return_type = proto_tc->req_method_ret[proto_rm];
        int rsid = (proto_tc->req_method_ret_is_self[proto_rm] == VM_TRUE)
                   ? c->current_struct_id
                   : proto_tc->req_method_ret_sid[proto_rm];
        fn->return_struct_id = (vm_i16)rsid;
        if (rsid >= 0) {
            if (c->structs[rsid].num_fields > 0) {
                fn->return_type = c->structs[rsid].field_types[0];
            }
            fn->return_reg_count = c->structs[rsid].num_fields;
        }
        fn->return_elem_types[0] = fn->return_type;
    }

    if (custom_name != VM_NULL && vm_str_eq_cstr(custom_name, vm_str_len(custom_name), "op_as") == VM_TRUE) {
        char as_name[48];
        if (fn->return_struct_id >= 0) {
            const char *sname = c->structs[fn->return_struct_id].name;
            vm_u32 slen = vm_str_len(sname);
            vm_str_copy(as_name, "op_as_", 6, 48);
            for (vm_u32 k = 0; k < slen && (6 + k) < 47; k++) as_name[6 + k] = sname[k];
            as_name[6 + slen] = '\0';
        } else if (fn->return_type == VM_TYPE_STRING) {
            vm_str_copy(as_name, "op_str", 6, 48);
        } else if (fn->return_type == VM_TYPE_I64) {
            vm_str_copy(as_name, "op_as_i64", 9, 48);
        } else if (fn->return_type == VM_TYPE_F32) {
            vm_str_copy(as_name, "op_as_f32", 9, 48);
        } else if (fn->return_type == VM_TYPE_F64) {
            vm_str_copy(as_name, "op_as_f64", 9, 48);
        } else if (fn->return_type == VM_TYPE_BOOL) {
            vm_str_copy(as_name, "op_as_bool", 10, 48);
        } else {
            vm_str_copy(as_name, "op_as_i32", 9, 48);
        }
        if (prefix != VM_NULL && prefix[0] != '\0') {
            vm_str_join_dot(fn->name, 64, prefix, vm_str_len(prefix), as_name, vm_str_len(as_name));
        } else {
            vm_str_copy(fn->name, as_name, vm_str_len(as_name), 64);
        }
        c_func_index_rename(c, fid);
        fn_name_len = vm_str_len(fn->name);
    }

    /* Forward declaration without `{ ... }` body (C `.h` style prototype) */
    if (c_check(c, TOK_LBRACE) == VM_FALSE) {
        fn->start_ip = 0xFFFFFFFFu;
        fn->num_regs = c->max_reg;
        (void)c_check_duplicate_func_overload(c, (int)fid);
        c_match(c, TOK_COMMA);
        c_match(c, TOK_SEMICOLON);
        c->current_struct_id = saved_struct_id;
        return;
    }

    /* If implementing a previously forward-declared prototype (`start_ip == 0xFFFFFFFFu`), reuse its slot! */
    int reuse_fid = -1;
    for (int i = c_func_first_named(c, fn->name, fn_name_len); i >= 0 && i < (int)fid;
         i = c_func_next_named(c, i, fn->name, fn_name_len)) {
        VMFunctionInfo *ef = &c->chunk->functions[i];
        if (ef->start_ip != 0xFFFFFFFFu || ef->arity != fn->arity) continue;
        if (vm_str_eq(ef->name, vm_str_len(ef->name), fn->name, fn_name_len) == VM_FALSE) continue;
        vm_bool same = VM_TRUE;
        for (vm_u8 p = 0; p < fn->arity; p++) {
            if (ef->param_types[p] != fn->param_types[p] ||
                ef->param_struct_ids[p] != fn->param_struct_ids[p]) {
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
        c->chunk->num_functions--;
        fid = (vm_u32)reuse_fid;
        fn = &c->chunk->functions[fid];
    } else {
        (void)c_check_duplicate_func_overload(c, (int)fid);
    }

    int saved_func_id = c->current_func_id;
    c->current_func_id = (int)fid;
    c->current_ret_type = fn->return_type;
    c->current_ret_struct_id = fn->return_struct_id;
    fn->start_ip = c->chunk->code_count;

    if ((prefix == VM_NULL || prefix[0] == '\0') && custom_name == VM_NULL &&
        vm_str_eq_cstr(name.start, name.length, "main") == VM_TRUE) {
        c->chunk->entry_func_id = (vm_i32)fid;
    }

    c_compile_block(c);
    c->current_func_id = saved_func_id;

    if (c->chunk->entry_func_id == (vm_i32)fid) {
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_HALT, 0, 0, 0));
    } else {
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, 0, fn->return_reg_count, 0));
    }
    fn->num_regs = c->max_reg;

    /* Link any matching `type` contract layout forward method (`VecOps.<mname>`) to this concrete implementation */
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
                if (lf->start_ip == 0xFFFFFFFFu && lf->arity == fn->arity &&
                    vm_str_eq(lf->name, vm_str_len(lf->name), lqname, lqlen) == VM_TRUE) {
                    lf->start_ip = fn->start_ip;
                    lf->num_regs = fn->num_regs;
                }
            }
            if (tc->last_impl_struct_id < 0 && c_does_struct_implement_type(c, impl_sid, t) == VM_TRUE) {
                tc->last_impl_struct_id = impl_sid;
            }
        }
    }

    c->current_struct_id = saved_struct_id;
}
