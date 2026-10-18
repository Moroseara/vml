#include "decl.h"
#include "../module/module.h"
#include "../expr/expr.h"
#include "../stmt/stmt.h"

/* Parses `as math` or `as std.math`, returning the final namespace token (`math`) */
static Token parse_namespace_target(Compiler *c) {
    Token ns = c->cur;
    if (c->cur.type == TOK_IDENT || (c->cur.type >= TOK_TYPE_I32 && c->cur.type <= TOK_TYPE_VOID)) {
        c_advance(c);
    } else {
        c_consume(c, TOK_IDENT, "Expected namespace name after 'as'");
    }
    while (c_match(c, TOK_DOT) == VM_TRUE) {
        ns = c->cur;
        if (c->cur.type == TOK_IDENT || (c->cur.type >= TOK_TYPE_I32 && c->cur.type <= TOK_TYPE_VOID)) {
            c_advance(c);
        } else {
            c_consume(c, TOK_IDENT, "Expected identifier after '.' in namespace");
        }
    }
    return ns;
}

/*
 * Compiles `const` / `export const data: int = 0` / `export const data: int = 0 as std.math`
 */
void c_compile_const_decl(Compiler *c, const char *prefix, vm_bool is_exported) {
    Token name = c->cur;
    c_consume(c, TOK_IDENT, "Expected constant name");
    VMDataType type = VM_TYPE_I32;
    vm_bool has_type = VM_FALSE;
    if (c_match(c, TOK_COLON) == VM_TRUE) {
        type = c_parse_type(c);
        has_type = VM_TRUE;
    }
    c_consume(c, TOK_ASSIGN, "Expected '=' in constant declaration");

    vm_u32 saved_count = c->chunk->code_count;
    ExprResult val = c_compile_expr(c, type);
    c->chunk->code_count = saved_count;
    if (has_type == VM_FALSE) type = val.type;

    /* Optional trailing `as std.math` on `export const data: int = 0 as std.math` */
    char custom_ns[32];
    custom_ns[0] = '\0';
    if (c_match(c, TOK_AS) == VM_TRUE) {
        Token ns_tok = parse_namespace_target(c);
        vm_str_copy(custom_ns, ns_tok.start, ns_tok.length, 32);
    }

    const char *active_prefix = (custom_ns[0] != '\0') ? custom_ns : prefix;

    GlobalConst *gc = &c->global_consts[c->num_global_consts++];
    if (active_prefix != VM_NULL && active_prefix[0] != '\0') {
        vm_str_join_dot(gc->name, 64, active_prefix, vm_str_len(active_prefix), name.start, name.length);
    } else {
        vm_str_copy(gc->name, name.start, name.length, 64);
    }
    gc->length = vm_str_len(gc->name);
    gc->type = type;
    gc->val = val.const_val;

    if (is_exported == VM_TRUE) {
        GlobalConst *direct = &c->global_consts[c->num_global_consts++];
        vm_str_copy(direct->name, name.start, name.length, 64);
        direct->length = name.length;
        direct->type = type;
        direct->val = val.const_val;

        if (c->module_prefix[0] != '\0') {
            GlobalConst *modc = &c->global_consts[c->num_global_consts++];
            vm_str_join_dot(modc->name, 64, c->module_prefix, vm_str_len(c->module_prefix), name.start, name.length);
            modc->length = vm_str_len(modc->name);
            modc->type = type;
            modc->val = val.const_val;
        }
    }
    c_match(c, TOK_SEMICOLON);
}

static void register_enum_variant_entry(Compiler *c, const char *prefix, vm_u32 plen,
                                        Token vname, vm_i32 tag, vm_bool has_payload, VMDataType ptype) {
    GlobalConst *gc = &c->global_consts[c->num_global_consts++];
    vm_str_join_dot(gc->name, 64, prefix, plen, vname.start, vname.length);
    gc->length = vm_str_len(gc->name);
    gc->type = VM_TYPE_I32;
    gc->val.i32 = tag;

    if (c->num_enum_variants < 128) {
        EnumVariant *ev = &c->enum_variants[c->num_enum_variants++];
        vm_str_copy(ev->qname, gc->name, gc->length, 64);
        ev->tag = tag;
        ev->has_payload = has_payload;
        ev->is_tagged_enum = has_payload;
        ev->payload_type = ptype;
    }
}

void c_compile_enum_decl(Compiler *c, vm_bool is_exported) {
    Token ename = c->cur;
    c_consume(c, TOK_IDENT, "Expected enum name");

    Token export_alias = ename;
    vm_bool has_as = VM_FALSE;
    if (c_match(c, TOK_AS) == VM_TRUE) {
        export_alias = parse_namespace_target(c);
        has_as = VM_TRUE;
    }

    c_consume(c, TOK_LBRACE, "Expected '{' after enum name");
    vm_i32 val = 0;
    int first_ev_idx = c->num_enum_variants;
    vm_bool any_payload = VM_FALSE;
    VMDataType default_ptype = VM_TYPE_I32;

    while (c_check(c, TOK_RBRACE) == VM_FALSE && c_check(c, TOK_EOF) == VM_FALSE) {
        Token vname = c->cur;
        c_consume(c, TOK_IDENT, "Expected enum variant name");

        vm_bool has_payload = VM_FALSE;
        VMDataType ptype = VM_TYPE_I32;
        if (c_match(c, TOK_LPAREN) == VM_TRUE) {
            has_payload = VM_TRUE;
            any_payload = VM_TRUE;
            ptype = c_parse_type(c);
            default_ptype = ptype;
            c_consume(c, TOK_RPAREN, "Expected ')' after enum variant payload type");
        } else if (c_match(c, TOK_ASSIGN) == VM_TRUE && c_check(c, TOK_INT_LIT) == VM_TRUE) {
            val = (vm_i32)c->cur.int_val;
            c_advance(c);
        }

        vm_i32 cur_tag = val++;
        register_enum_variant_entry(c, ename.start, ename.length, vname, cur_tag, has_payload, ptype);
        if ((is_exported == VM_TRUE || has_as == VM_TRUE) &&
            vm_str_eq(ename.start, ename.length, export_alias.start, export_alias.length) == VM_FALSE) {
            register_enum_variant_entry(c, export_alias.start, export_alias.length, vname, cur_tag, has_payload, ptype);
        }
        c_match(c, TOK_COMMA);
    }
    c_consume(c, TOK_RBRACE, "Expected '}' after enum");

    if (c_match(c, TOK_AS) == VM_TRUE) {
        export_alias = parse_namespace_target(c);
        has_as = VM_TRUE;
    }

    if (any_payload == VM_TRUE) {
        for (int i = first_ev_idx; i < c->num_enum_variants; i++) {
            c->enum_variants[i].is_tagged_enum = VM_TRUE;
            if (c->enum_variants[i].has_payload == VM_FALSE) {
                c->enum_variants[i].payload_type = default_ptype;
            }
        }
    }
}

const char *c_parse_operator_name(Compiler *c) {
    const char *op_name = "op+";
    if (c_match(c, TOK_PLUS) == VM_TRUE)             op_name = "op+";
    else if (c_match(c, TOK_MINUS) == VM_TRUE)       op_name = "op-";
    else if (c_match(c, TOK_STAR) == VM_TRUE)        op_name = "op*";
    else if (c_match(c, TOK_SLASH) == VM_TRUE)       op_name = "op/";
    else if (c_match(c, TOK_PERCENT) == VM_TRUE)     op_name = "op%";
    else if (c_match(c, TOK_EQ) == VM_TRUE)          op_name = "op==";
    else if (c_match(c, TOK_NE) == VM_TRUE)          op_name = "op!=";
    else if (c_match(c, TOK_LT) == VM_TRUE)          op_name = "op<";
    else if (c_match(c, TOK_LE) == VM_TRUE)          op_name = "op<=";
    else if (c_match(c, TOK_GT) == VM_TRUE)          op_name = "op>";
    else if (c_match(c, TOK_GE) == VM_TRUE)          op_name = "op>=";
    else if (c_match(c, TOK_BAND) == VM_TRUE)        op_name = "op&";
    else if (c_match(c, TOK_BOR) == VM_TRUE)         op_name = "op|";
    else if (c_match(c, TOK_BXOR) == VM_TRUE)        op_name = "op^";
    else if (c_match(c, TOK_BNOT) == VM_TRUE)        op_name = "op~";
    else if (c_match(c, TOK_SHL) == VM_TRUE)         op_name = "op<<";
    else if (c_match(c, TOK_SHR) == VM_TRUE)         op_name = "op>>";
    else if (c_match(c, TOK_HASH) == VM_TRUE || c_match(c, TOK_LEN) == VM_TRUE) op_name = "op_len";
    else if (c_match(c, TOK_IN) == VM_TRUE)          op_name = "op_in";
    else if (c_match(c, TOK_IS) == VM_TRUE)          op_name = "op_is";
    else if (c_match(c, TOK_AS) == VM_TRUE ||
             c_match(c, TOK_TYPE_I32) == VM_TRUE ||
             c_match(c, TOK_TYPE_I64) == VM_TRUE ||
             c_match(c, TOK_TYPE_F32) == VM_TRUE ||
             c_match(c, TOK_TYPE_F64) == VM_TRUE ||
             c_match(c, TOK_TYPE_BOOL) == VM_TRUE)   op_name = "op_as";
    else if (c_match(c, TOK_TYPE_STRING) == VM_TRUE) op_name = "op_str";
    else if (c_check(c, TOK_IDENT) == VM_TRUE &&
             vm_str_eq_cstr(c->cur.start, c->cur.length, "iter") == VM_TRUE) {
        c_advance(c);
        op_name = "op_iter";
    } else if (c_match(c, TOK_LPAREN) == VM_TRUE && c_match(c, TOK_RPAREN) == VM_TRUE) {
        op_name = "op_call";
    } else if (c_match(c, TOK_LBRACKET) == VM_TRUE) {
        c_consume(c, TOK_RBRACKET, "Expected ']' in operator []");
        if (c_match(c, TOK_ASSIGN) == VM_TRUE) op_name = "op[]=";
        else op_name = "op[]";
    } else {
        c_error(c, "Unsupported operator in operator overload");
    }
    return op_name;
}

/* Compiles out-of-line `operator Vec2.+ (a: Self, b: Self): Self { ... }` */
void c_compile_top_level_operator(Compiler *c) {
    int saved_sid = c->current_struct_id;
    const char *prefix = VM_NULL;
    if (c_check(c, TOK_IDENT) == VM_TRUE && lexer_peek(&c->lex).type == TOK_DOT) {
        Token sname = c->cur;
        c_advance(c);
        c_advance(c); /* consume '.' */
        int sid = c_resolve_struct(c, sname.start, sname.length);
        if (sid < 0) {
            c_error(c, "Unknown struct in top-level operator declaration");
        } else {
            c->current_struct_id = sid;
            prefix = c->structs[sid].name;
        }
    }
    const char *op_name = c_parse_operator_name(c);
    c_compile_func_or_method(c, prefix, op_name, VM_TRUE);
    c->current_struct_id = saved_sid;
}

static vm_bool is_func_type_signature_after_assign(Compiler *c) {
    if (c_check(c, TOK_LPAREN) == VM_FALSE) return VM_FALSE;
    Lexer lcopy = c->lex;
    Token t1 = lexer_next(&lcopy);
    if (t1.type == TOK_RPAREN) return VM_TRUE;
    if (t1.type == TOK_IDENT) {
        Token t2 = lexer_next(&lcopy);
        if (t2.type == TOK_COLON) return VM_TRUE;
    }
    int depth = 1;
    Token tok = t1;
    while (tok.type != TOK_EOF && depth > 0) {
        if (tok.type == TOK_LPAREN) depth++;
        else if (tok.type == TOK_RPAREN) {
            depth--;
            if (depth == 0) break;
        }
        tok = lexer_next(&lcopy);
    }
    Token after = lexer_next(&lcopy);
    return vm_bool_from_int(after.type == TOK_COLON);
}

/*
 * Compiles:
 * 1. `type Score = float` (type alias)
 * 2. `type func step_a(n: int): int` / `type step_a = (n: int): int` (C `.h` function prototype & callback type)
 * 3. `type struct VecOps { ... }` / `type VecOps { ... }`
 *    (header contract mixing signatures, operators, and optional `{ ... }` default bodies)
 */
void c_compile_type_decl(Compiler *c, vm_bool is_exported) {
    (void)is_exported;
    vm_bool explicit_func = c_match(c, TOK_FUNC);
    vm_bool explicit_struct = (explicit_func == VM_FALSE) ? c_match(c, TOK_STRUCT) : VM_FALSE;
    Token name = c->cur;
    c_consume(c, TOK_IDENT, "Expected type name");

    int existing_tid = c_resolve_type_contract(c, name.start, name.length);
    int tid = (existing_tid >= 0) ? existing_tid : c->num_type_contracts++;
    TypeContract *tc = &c->type_contracts[tid];
    if (existing_tid < 0) {
        vm_str_copy(tc->name, name.start, name.length, 64);
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
    }

    if (explicit_func == VM_TRUE || (explicit_struct == VM_FALSE && c_match(c, TOK_ASSIGN) == VM_TRUE)) {
        if (explicit_func == VM_TRUE) {
            c_match(c, TOK_ASSIGN);
        }
        if (explicit_func == VM_TRUE || is_func_type_signature_after_assign(c) == VM_TRUE) {
            tc->is_func_type = VM_TRUE;
            c_consume(c, TOK_LPAREN, "Expected '(' in function type");
            vm_u8 arity = 0;
            VMDataType ptypes[8];
            int psids[8];
            if (c_check(c, TOK_RPAREN) == VM_FALSE) {
                do {
                    if (c_check(c, TOK_IDENT) == VM_TRUE && lexer_peek(&c->lex).type == TOK_COLON) {
                        c_advance(c);
                        c_advance(c);
                    }
                    int psid = -1;
                    VMDataType pt = c_parse_type_ex(c, &psid);
                    if (arity < 8) {
                        ptypes[arity] = pt;
                        psids[arity] = psid;
                    }
                    arity++;
                } while (c_match(c, TOK_COMMA) == VM_TRUE);
            }
            c_consume(c, TOK_RPAREN, "Expected ')' after function type parameters");
            int rsid = -1;
            VMDataType rtype = VM_TYPE_VOID;
            if (c_match(c, TOK_COLON) == VM_TRUE) {
                rtype = c_parse_type_ex(c, &rsid);
            }
            tc->func_arity = arity;
            for (vm_u8 i = 0; i < arity && i < 8; i++) {
                tc->func_param_types[i] = ptypes[i];
                tc->func_param_sids[i] = psids[i];
            }
            tc->func_ret_type = rtype;
            tc->func_ret_sid = rsid;

            /* Pre-allocate forward `VMFunctionInfo` slot so `name(...)` can be called before `func name` is defined! */
            char fqname[64];
            if (c->module_prefix[0] != '\0') {
                vm_str_join_dot(fqname, 64, c->module_prefix, vm_str_len(c->module_prefix), name.start, name.length);
            } else {
                vm_str_copy(fqname, name.start, name.length, 64);
            }
            int existing_fid = c_resolve_func_overload(c, fqname, vm_str_len(fqname), ptypes, psids, arity);
            if (existing_fid < 0) {
                vm_u32 fid = c_alloc_func_slot(c);
                VMFunctionInfo *fn = &c->chunk->functions[fid];
                vm_str_copy(fn->name, fqname, vm_str_len(fqname), 64);
                fn->arity = arity;
                fn->min_arity = arity;
                fn->is_pub = VM_TRUE;
                fn->is_deprecated = VM_FALSE;
                fn->coerce_args_to_string = VM_FALSE;
                fn->deprecated_msg[0] = '\0';
                fn->owner_struct_id = -1;
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
            } else {
                tc->forward_func_id = existing_fid;
            }
            c_match(c, TOK_SEMICOLON);
            return;
        }

        tc->is_alias = VM_TRUE;
        tc->alias_type = c_parse_type_ex(c, &tc->alias_struct_id);
        c_match(c, TOK_SEMICOLON);
        return;
    }

    /* Create backing layout `StructDef` for `type VecOps { ... }` */
    int lsid = c->num_structs++;
    StructDef *lsd = &c->structs[lsid];
    vm_str_copy(lsd->name, name.start, name.length, 64);
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

    c_consume(c, TOK_LBRACE, "Expected '{' or '=' in type declaration");
    while (c_check(c, TOK_RBRACE) == VM_FALSE && c_check(c, TOK_EOF) == VM_FALSE) {
        c_parse_attributes(c);
        c_match(c, TOK_PUB);
        c_match(c, TOK_EXPORT);

        vm_bool is_op = c_match(c, TOK_OPERATOR);
        vm_bool has_func_kw = (is_op == VM_FALSE) ? c_match(c, TOK_FUNC) : VM_FALSE;
        const char *op_str = VM_NULL;
        Token mname = c->cur;

        if (is_op == VM_TRUE) {
            op_str = c_parse_operator_name(c);
            c_match(c, TOK_COLON);
        } else {
            if (c->cur.type == TOK_IDENT || c->cur.type == TOK_LEN ||
                (c->cur.type >= TOK_TYPE_I32 && c->cur.type <= TOK_TYPE_VOID)) {
                c_advance(c);
            } else {
                c_consume(c, TOK_IDENT, "Expected field or method name in type declaration");
            }
            if (has_func_kw == VM_FALSE && c_check(c, TOK_LPAREN) == VM_FALSE) {
                c_consume(c, TOK_COLON, "Expected ':' after member name in type declaration");
            } else {
                c_match(c, TOK_COLON);
            }
        }

        if (is_op == VM_TRUE || has_func_kw == VM_TRUE || c_check(c, TOK_LPAREN) == VM_TRUE) {
            const char *sig_start = c->cur.start;
            c_consume(c, TOK_LPAREN, "Expected '(' in type method/operator signature");
            vm_u8 arity = 0;
            VMDataType ptypes[8];
            int psids[8];
            vm_bool pis_self[8];
            if (c_check(c, TOK_RPAREN) == VM_FALSE) {
                do {
                    if (c_check(c, TOK_IDENT) == VM_TRUE && lexer_peek(&c->lex).type == TOK_COLON) {
                        c_advance(c);
                        c_advance(c);
                    }
                    vm_bool self_tok = vm_bool_from_int(
                        c_check(c, TOK_SELF_TYPE) == VM_TRUE ||
                        (c_check(c, TOK_IDENT) == VM_TRUE &&
                         vm_str_eq(c->cur.start, c->cur.length, name.start, name.length) == VM_TRUE));
                    int psid = -1;
                    VMDataType pt = VM_TYPE_I32;
                    if (c_check(c, TOK_COMMA) == VM_FALSE && c_check(c, TOK_RPAREN) == VM_FALSE) {
                        pt = c_parse_type_ex(c, &psid);
                    } else {
                        self_tok = VM_TRUE;
                        psid = lsid;
                    }
                    if (arity < 8) {
                        ptypes[arity] = pt;
                        psids[arity] = psid;
                        pis_self[arity] = self_tok;
                    }
                    arity++;
                } while (c_match(c, TOK_COMMA) == VM_TRUE);
            }
            c_consume(c, TOK_RPAREN, "Expected ')' after type method parameters");
            VMDataType rtype = VM_TYPE_VOID;
            int rsid = -1;
            vm_bool ret_self = VM_FALSE;
            if (c_match(c, TOK_COLON) == VM_TRUE) {
                ret_self = vm_bool_from_int(
                    c_check(c, TOK_SELF_TYPE) == VM_TRUE ||
                    (c_check(c, TOK_IDENT) == VM_TRUE &&
                     vm_str_eq(c->cur.start, c->cur.length, name.start, name.length) == VM_TRUE));
                rtype = c_parse_type_ex(c, &rsid);
            }

            char final_mname[32];
            if (is_op == VM_TRUE) {
                if (vm_str_eq_cstr(op_str, vm_str_len(op_str), "op_as") == VM_TRUE) {
                    if (rsid >= 0) {
                        const char *sn = c->structs[rsid].name;
                        vm_u32 sl = vm_str_len(sn);
                        vm_str_copy(final_mname, "op_as_", 6, 32);
                        for (vm_u32 k = 0; k < sl && (6 + k) < 31; k++) final_mname[6 + k] = sn[k];
                        final_mname[6 + sl] = '\0';
                    } else if (rtype == VM_TYPE_STRING) vm_str_copy(final_mname, "op_str", 6, 32);
                    else if (rtype == VM_TYPE_I64) vm_str_copy(final_mname, "op_as_i64", 9, 32);
                    else if (rtype == VM_TYPE_F32) vm_str_copy(final_mname, "op_as_f32", 9, 32);
                    else if (rtype == VM_TYPE_F64) vm_str_copy(final_mname, "op_as_f64", 9, 32);
                    else if (rtype == VM_TYPE_BOOL) vm_str_copy(final_mname, "op_as_bool", 10, 32);
                    else vm_str_copy(final_mname, "op_as_i32", 9, 32);
                } else {
                    vm_str_copy(final_mname, op_str, vm_str_len(op_str), 32);
                }
            } else {
                vm_str_copy(final_mname, mname.start, mname.length, 32);
            }

            const char *body_src = VM_NULL;
            if (c_check(c, TOK_LBRACE) == VM_TRUE) {
                body_src = sig_start;
                c_skip_block_tokens(c);
            }

            if (tc->num_req_methods < 16) {
                vm_u8 midx = tc->num_req_methods++;
                vm_str_copy(tc->req_method_names[midx], final_mname, vm_str_len(final_mname), 32);
                tc->req_method_arity[midx] = arity;
                for (vm_u8 i = 0; i < arity && i < 8; i++) {
                    tc->req_method_param_types[midx][i] = ptypes[i];
                    tc->req_method_param_sids[midx][i] = psids[i];
                    tc->req_method_param_is_self[midx][i] = pis_self[i];
                }
                tc->req_method_ret[midx] = rtype;
                tc->req_method_ret_sid[midx] = rsid;
                tc->req_method_ret_is_self[midx] = ret_self;
                tc->req_method_body_src[midx] = body_src;
            }
        } else {
            VMDataType ftype = c_parse_type(c);
            vm_bool has_def = VM_FALSE;
            VMReg def_val = {0};
            if (c_match(c, TOK_ASSIGN) == VM_TRUE) {
                vm_u32 saved_cnt = c->chunk->code_count;
                vm_u8 saved_r = c->next_reg;
                ExprResult dv = c_compile_expr(c, ftype);
                c->chunk->code_count = saved_cnt;
                c->next_reg = saved_r;
                has_def = VM_TRUE;
                def_val = dv.const_val;
            }
            if (tc->num_req_fields < 16) {
                vm_u8 fidx = tc->num_req_fields++;
                vm_str_copy(tc->req_field_names[fidx], mname.start, mname.length, 32);
                tc->req_field_types[fidx] = ftype;
                tc->req_field_has_default[fidx] = has_def;
                tc->req_field_defaults[fidx] = def_val;
            }
            if (lsd->num_fields < MAX_STRUCT_FIELDS) {
                vm_u8 lfidx = lsd->num_fields++;
                vm_str_copy(lsd->field_names[lfidx], mname.start, mname.length, 32);
                lsd->field_types[lfidx] = ftype;
                lsd->field_is_pub[lfidx] = VM_TRUE;
                lsd->field_has_default[lfidx] = has_def;
                lsd->field_defaults[lfidx] = def_val;
            }
        }
        c_match(c, TOK_COMMA);
        c_match(c, TOK_SEMICOLON);
    }
    c_consume(c, TOK_RBRACE, "Expected '}' after type declaration");

    if (lsd->num_fields == 0) {
        lsd->num_fields = 1;
        lsd->field_names[0][0] = '\0';
        lsd->field_types[0] = VM_TYPE_I32;
        lsd->field_is_pub[0] = VM_FALSE;
        lsd->field_has_default[0] = VM_FALSE;
    }

    /* Update `Self` types and pre-register forward/default methods on `lsd` (`VecOps.<mname>`) */
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
        if (tc->req_method_body_src[rm] != VM_NULL) {
            Lexer saved_lex = c->lex;
            Token saved_cur = c->cur;
            c->current_struct_id = lsid;
            lexer_init(&c->lex, tc->req_method_body_src[rm]);
            c_advance(c);
            c_compile_func_or_method(c, lsd->name, tc->req_method_names[rm], VM_TRUE);
            c->lex = saved_lex;
            c->cur = saved_cur;
        } else {
            vm_u32 fid = c_alloc_func_slot(c);
            VMFunctionInfo *fn = &c->chunk->functions[fid];
            vm_str_join_dot(fn->name, 64, lsd->name, vm_str_len(lsd->name),
                            tc->req_method_names[rm], vm_str_len(tc->req_method_names[rm]));
            fn->arity = tc->req_method_arity[rm];
            fn->min_arity = tc->req_method_arity[rm];
            fn->is_pub = VM_TRUE;
            fn->is_deprecated = VM_FALSE;
            fn->coerce_args_to_string = VM_FALSE;
            fn->deprecated_msg[0] = '\0';
            fn->owner_struct_id = (vm_i16)lsid;
            for (vm_u8 p = 0; p < fn->arity && p < 8; p++) {
                fn->param_types[p] = tc->req_method_param_types[rm][p];
                fn->param_struct_ids[p] = (vm_i16)tc->req_method_param_sids[rm][p];
                fn->param_has_default[p] = VM_FALSE;
            }
            fn->return_type = tc->req_method_ret[rm];
            fn->return_struct_id = (vm_i16)tc->req_method_ret_sid[rm];
            fn->return_reg_count = (fn->return_struct_id >= 0)
                                   ? c->structs[fn->return_struct_id].num_fields : 1;
            fn->return_elem_types[0] = fn->return_type;
            fn->start_ip = 0xFFFFFFFFu;
            fn->num_regs = fn->arity;
        }
    }

    c->current_struct_id = saved_struct_id;
}

void c_compile_struct_or_type_decl(Compiler *c, vm_bool is_exported) {
    Token name = c->cur;
    c_consume(c, TOK_IDENT, "Expected struct/type name");

    int sid = c->num_structs++;
    StructDef *sd = &c->structs[sid];
    vm_str_copy(sd->name, name.start, name.length, 64);
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

    if (c_match(c, TOK_LT) == VM_TRUE) {
        sd->is_generic = VM_TRUE;
        do {
            Token tp = c->cur;
            c_consume(c, TOK_IDENT, "Expected generic parameter name");
            if (sd->num_type_params == 0) {
                vm_str_copy(sd->type_param_name, tp.start, tp.length, 32);
            }
            sd->num_type_params++;
            c_match(c, TOK_ELLIPSIS);
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
        c_consume(c, TOK_GT, "Expected '>'");
    }

    /* Duplicate struct / generic struct arity collision check */
    for (int i = 0; i < sid; i++) {
        if (c->structs[i].owner_type_id >= 0) continue;
        vm_u32 slen = vm_str_len(c->structs[i].name);
        if (vm_str_eq(c->structs[i].name, slen, sd->name, name.length) == VM_TRUE &&
            c->structs[i].num_type_params == sd->num_type_params) {
            vm_bool is_prim_struct = VM_FALSE;
            for (int pt = 0; pt < VM_TYPE_COUNT; pt++) {
                const char *pname = c->chunk->primitive_struct_name[pt];
                if (pname[0] != '\0' && vm_str_eq(sd->name, name.length, pname, vm_str_len(pname)) == VM_TRUE) {
                    is_prim_struct = VM_TRUE;
                    break;
                }
            }
            if (is_prim_struct == VM_TRUE || vm_str_eq_cstr(sd->name, name.length, "String") == VM_TRUE) {
                c->num_structs--;
                sid = i;
                sd = &c->structs[sid];
                break;
            }
            c_error(c, "Duplicate struct or generic struct with identical type parameter count");
        }
    }

    if (c_match(c, TOK_COLON) == VM_TRUE) {
        do {
            Token parent_tok = c->cur;
            c_consume(c, TOK_IDENT, "Expected parent struct or type contract name");
            int tid = c_resolve_type_contract(c, parent_tok.start, parent_tok.length);
            int pid = (tid < 0) ? c_resolve_struct(c, parent_tok.start, parent_tok.length) : -1;
            if (pid >= 0) {
                if (sd->parent_struct_id < 0) sd->parent_struct_id = pid;
                if (sd->num_parents < 4) sd->parent_struct_ids[sd->num_parents++] = pid;
                StructDef *parent = &c->structs[pid];
                for (vm_u8 f = 0; f < parent->num_fields; f++) {
                    if (parent->field_names[f][0] == '\0') continue;
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
                /* Auto-inherit fields (and their defaults) declared in the `type` contract! */
                TypeContract *tc = &c->type_contracts[tid];
                for (vm_u8 rf = 0; rf < tc->num_req_fields; rf++) {
                    vm_bool exists = VM_FALSE;
                    vm_u32 rflen = vm_str_len(tc->req_field_names[rf]);
                    for (vm_u8 sf = 0; sf < sd->num_fields; sf++) {
                        if (vm_str_eq(sd->field_names[sf], vm_str_len(sd->field_names[sf]),
                                      tc->req_field_names[rf], rflen) == VM_TRUE) {
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
                c_error(c, "Unknown parent struct or type contract");
            }
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
    }

    Token export_alias = name;
    vm_bool has_as_alias = VM_FALSE;
    if (c_match(c, TOK_AS) == VM_TRUE) {
        export_alias = parse_namespace_target(c);
        has_as_alias = VM_TRUE;
    }

    c->current_struct_id = sid;
    c_consume(c, TOK_LBRACE, "Expected '{'");

    while (c_check(c, TOK_RBRACE) == VM_FALSE && c_check(c, TOK_EOF) == VM_FALSE) {
        c_parse_attributes(c);
        vm_bool member_pub = VM_FALSE;
        if (c_match(c, TOK_PUB) == VM_TRUE || c_match(c, TOK_EXPORT) == VM_TRUE) {
            member_pub = VM_TRUE;
        }
        if (c_match(c, TOK_FUNC) == VM_TRUE) {
            c_compile_func_or_method(c, sd->name, VM_NULL, member_pub);
        } else if (c_match(c, TOK_CONST) == VM_TRUE) {
            c_compile_const_decl(c, sd->name, vm_bool_from_int(is_exported == VM_TRUE && member_pub == VM_TRUE));
        } else if (c_match(c, TOK_OPERATOR) == VM_TRUE) {
            const char *op_name = c_parse_operator_name(c);
            c_compile_func_or_method(c, sd->name, op_name, VM_TRUE);
        } else {
            Token fname = c->cur;
            c_consume(c, TOK_IDENT, "Expected field name");
            c_consume(c, TOK_COLON, "Expected ':'");
            VMDataType ftype = c_parse_type(c);

            /* Check if field was already inherited from a bound `type` contract or parent `struct` */
            int existing_fidx = -1;
            for (vm_u8 sf = 0; sf < sd->num_fields; sf++) {
                if (vm_str_eq(sd->field_names[sf], vm_str_len(sd->field_names[sf]),
                              fname.start, fname.length) == VM_TRUE) {
                    existing_fidx = (int)sf;
                    break;
                }
            }
            vm_u8 fidx = (existing_fidx >= 0) ? (vm_u8)existing_fidx : sd->num_fields++;
            if (existing_fidx < 0) {
                vm_str_copy(sd->field_names[fidx], fname.start, fname.length, 32);
                sd->field_has_default[fidx] = VM_FALSE;
                sd->field_defaults[fidx] = (VMReg){0};
            }
            sd->field_types[fidx] = ftype;
            sd->field_is_pub[fidx] = member_pub;

            if (c_match(c, TOK_ASSIGN) == VM_TRUE) {
                vm_u32 saved_cnt = c->chunk->code_count;
                vm_u8 saved_r = c->next_reg;
                ExprResult def_v = c_compile_expr(c, ftype);
                c->chunk->code_count = saved_cnt;
                c->next_reg = saved_r;
                sd->field_has_default[fidx] = VM_TRUE;
                sd->field_defaults[fidx] = def_v.const_val;
            }
            c_match(c, TOK_COMMA);
            c_match(c, TOK_SEMICOLON);
        }
    }

    c_consume(c, TOK_RBRACE, "Expected '}'");
    if (sd->num_fields == 0) {
        sd->num_fields = 1;
        sd->field_names[0][0] = '\0';
        sd->field_types[0] = VM_TYPE_I32;
        sd->field_is_pub[0] = VM_FALSE;
        sd->field_has_default[0] = VM_FALSE;
    }

    /* Auto-inherit default-implemented methods and operators from bound `type` contracts! */
    for (vm_u8 t = 0; t < sd->num_implemented_types; t++) {
        TypeContract *tc = &c->type_contracts[sd->implemented_types[t]];
        for (vm_u8 rf = 0; rf < tc->num_req_fields; rf++) {
            vm_bool found_f = VM_FALSE;
            vm_u32 rflen = vm_str_len(tc->req_field_names[rf]);
            for (vm_u8 sf = 0; sf < sd->num_fields; sf++) {
                vm_u32 sflen = vm_str_len(sd->field_names[sf]);
                if (vm_str_eq(sd->field_names[sf], sflen, tc->req_field_names[rf], rflen) == VM_TRUE &&
                    sd->field_types[sf] == tc->req_field_types[rf]) {
                    found_f = VM_TRUE;
                    break;
                }
            }
            if (found_f == VM_FALSE) {
                c_error(c, "Struct is missing a required field (or has mismatched type) from bound 'type' contract");
            }
        }
        for (vm_u8 rm = 0; rm < tc->num_req_methods; rm++) {
            if (tc->req_method_body_src[rm] == VM_NULL) continue;
            char mqname[64];
            vm_str_join_dot(mqname, 64, sd->name, vm_str_len(sd->name),
                            tc->req_method_names[rm], vm_str_len(tc->req_method_names[rm]));
            vm_u32 mqlen = vm_str_len(mqname);
            vm_bool has_override = VM_FALSE;
            for (vm_u32 f = 0; f < c->chunk->num_functions; f++) {
                if (vm_str_eq(c->chunk->functions[f].name, vm_str_len(c->chunk->functions[f].name),
                              mqname, mqlen) == VM_TRUE &&
                    c->chunk->functions[f].arity == tc->req_method_arity[rm]) {
                    has_override = VM_TRUE;
                    break;
                }
            }
            if (has_override == VM_FALSE) {
                Lexer saved_lex = c->lex;
                Token saved_cur = c->cur;
                c->current_struct_id = sid;
                lexer_init(&c->lex, tc->req_method_body_src[rm]);
                c_advance(c);
                c_compile_func_or_method(c, sd->name, tc->req_method_names[rm], VM_TRUE);
                c->lex = saved_lex;
                c->cur = saved_cur;
            }
        }
    }

    c->current_struct_id = -1;

    if (is_exported == VM_TRUE || has_as_alias == VM_TRUE) {
        if (has_as_alias == VM_TRUE) {
            c_export_struct_namespace(c, sd->name, vm_str_len(sd->name), export_alias.start, export_alias.length);
        } else if (c->module_prefix[0] != '\0') {
            vm_u32 mplen = vm_str_len(c->module_prefix);
            c_export_struct_namespace(c, sd->name, vm_str_len(sd->name), c->module_prefix, mplen - 1);
        } else {
            c_export_struct_namespace(c, sd->name, vm_str_len(sd->name), export_alias.start, export_alias.length);
        }
    }
}
