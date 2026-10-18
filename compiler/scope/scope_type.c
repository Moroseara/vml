#include "context.h"

static VMDataType parse_base_type_ex(Compiler *c, int *out_struct_id) {
    if (out_struct_id != VM_NULL) *out_struct_id = -1;
    if (c_match(c, TOK_TYPE_I32) == VM_TRUE)    return VM_TYPE_I32;
    if (c_match(c, TOK_TYPE_I64) == VM_TRUE)    return VM_TYPE_I64;
    if (c_match(c, TOK_TYPE_F32) == VM_TRUE)    return VM_TYPE_F32;
    if (c_match(c, TOK_TYPE_F64) == VM_TRUE)    return VM_TYPE_F64;
    if (c_match(c, TOK_TYPE_BOOL) == VM_TRUE)   return VM_TYPE_BOOL;
    if (c_match(c, TOK_TYPE_STRING) == VM_TRUE) return VM_TYPE_STRING;
    if (c_match(c, TOK_TYPE_VOID) == VM_TRUE)   return VM_TYPE_VOID;

    /* `Self` keyword: ONLY valid inside `struct` or `type` block! */
    if (c_match(c, TOK_SELF_TYPE) == VM_TRUE) {
        if (c->current_struct_id >= 0) {
            const char *sname = c->structs[c->current_struct_id].name;
            vm_u32 slen = vm_str_len(sname);
            for (int pt = 0; pt < VM_TYPE_COUNT; pt++) {
                const char *pname = c->chunk->primitive_struct_name[pt];
                if (pname[0] != '\0' && vm_str_eq(sname, slen, pname, vm_str_len(pname)) == VM_TRUE) {
                    return (VMDataType)pt;
                }
            }
            if (out_struct_id != VM_NULL) *out_struct_id = c->current_struct_id;
            return (c->structs[c->current_struct_id].num_fields > 0)
                   ? c->structs[c->current_struct_id].field_types[0] : VM_TYPE_I32;
        }
        return VM_TYPE_I32;
    }

    if (c_match(c, TOK_LPAREN) == VM_TRUE) {
        VMDataType first = c_parse_type(c);
        while (c_match(c, TOK_COMMA) == VM_TRUE) (void)c_parse_type(c);
        c_consume(c, TOK_RPAREN, "Expected ')' after tuple type");
        return first;
    }

    if (c_check(c, TOK_IDENT) == VM_TRUE) {
        Token id = c->cur;
        c_advance(c);
        c_match(c, TOK_ELLIPSIS);
        if (vm_str_eq_cstr(id.start, id.length, "array") == VM_TRUE) return VM_TYPE_ARRAY;
        if (vm_str_eq_cstr(id.start, id.length, "map") == VM_TRUE)   return VM_TYPE_MAP;
        for (int pt = 0; pt < VM_TYPE_COUNT; pt++) {
            const char *pname = c->chunk->primitive_struct_name[pt];
            if (pname[0] != '\0' && vm_str_eq(id.start, id.length, pname, vm_str_len(pname)) == VM_TRUE) {
                return (VMDataType)pt;
            }
        }
        int tid = c_resolve_type_contract(c, id.start, id.length);
        if (tid >= 0) {
            TypeContract *tc = &c->type_contracts[tid];
            if (tc->is_func_type == VM_TRUE) {
                return VM_TYPE_I32;
            }
            if (tc->is_alias == VM_TRUE) {
                if (out_struct_id != VM_NULL) *out_struct_id = tc->alias_struct_id;
                return tc->alias_type;
            }
            if (c->current_struct_id >= 0 &&
                c->structs[c->current_struct_id].owner_type_id != tid &&
                c_does_struct_implement_type(c, c->current_struct_id, tid) == VM_TRUE) {
                int csid = c->current_struct_id;
                if (out_struct_id != VM_NULL) *out_struct_id = csid;
                return (c->structs[csid].num_fields > 0) ? c->structs[csid].field_types[0] : VM_TYPE_I32;
            }
            if (tc->last_impl_struct_id >= 0) {
                int impl_sid = tc->last_impl_struct_id;
                if (out_struct_id != VM_NULL) *out_struct_id = impl_sid;
                return (c->structs[impl_sid].num_fields > 0) ? c->structs[impl_sid].field_types[0] : VM_TYPE_I32;
            }
            if (tc->layout_struct_id >= 0) {
                int lsid = tc->layout_struct_id;
                if (out_struct_id != VM_NULL) *out_struct_id = lsid;
                return (c->structs[lsid].num_fields > 0) ? c->structs[lsid].field_types[0] : VM_TYPE_I32;
            }
            return VM_TYPE_I32;
        }
        int sid = c_resolve_struct(c, id.start, id.length);
        if (sid >= 0) {
            if (out_struct_id != VM_NULL) *out_struct_id = sid;
            return (c->structs[sid].num_fields > 0) ? c->structs[sid].field_types[0] : VM_TYPE_I32;
        }
        for (vm_u8 i = 0; i < c->num_active_tparams; i++) {
            vm_u32 plen = vm_str_len(c->active_tparam_names[i]);
            if (vm_str_eq(c->active_tparam_names[i], plen, id.start, id.length) == VM_TRUE) {
                return c->active_tparam_types[i];
            }
        }
        return VM_TYPE_I32;
    }
    c_error(c, "Expected type annotation");
    return VM_TYPE_I32;
}

VMDataType c_parse_type_ex(Compiler *c, int *out_struct_id) {
    int base_sid = -1;
    VMDataType t = parse_base_type_ex(c, &base_sid);
    if (out_struct_id != VM_NULL) *out_struct_id = base_sid;
    c->last_parsed_elem_type = VM_TYPE_VOID;
    while (c_check(c, TOK_LBRACKET) == VM_TRUE && lexer_peek(&c->lex).type == TOK_RBRACKET) {
        c_advance(c);
        c_advance(c);
        if (out_struct_id != VM_NULL) *out_struct_id = -1;
        /* Struct element types are not tracked per-element yet; leave them unknown */
        c->last_parsed_elem_type = (base_sid >= 0) ? VM_TYPE_VOID : t;
        t = VM_TYPE_ARRAY;
    }
    return t;
}

VMDataType c_parse_type(Compiler *c) {
    return c_parse_type_ex(c, VM_NULL);
}

/*
 * Parses `@[inline]`, `@[deprecated("...")]`, `@[host_export("...")]` attributes
 */
void c_parse_attributes(Compiler *c) {
    while (c_match(c, TOK_AT_LBRACKET) == VM_TRUE) {
        do {
            Token attr = c->cur;
            c_consume(c, TOK_IDENT, "Expected attribute name inside '@[...]'");
            if (vm_str_eq_cstr(attr.start, attr.length, "deprecated") == VM_TRUE) {
                c->pending_deprecated = VM_TRUE;
                vm_str_copy(c->pending_deprecated_msg, "Function is marked @[deprecated]", 32, 64);
                if (c_match(c, TOK_LPAREN) == VM_TRUE) {
                    if (c_check(c, TOK_STRING_LIT) == VM_TRUE) {
                        vm_str_copy(c->pending_deprecated_msg, c->cur.start, c->cur.length, 64);
                        c_advance(c);
                    }
                    c_consume(c, TOK_RPAREN, "Expected ')' after @[deprecated(...)]");
                }
            } else if (vm_str_eq_cstr(attr.start, attr.length, "host_export") == VM_TRUE) {
                if (c_match(c, TOK_LPAREN) == VM_TRUE) {
                    if (c_check(c, TOK_STRING_LIT) == VM_TRUE) {
                        vm_str_copy(c->pending_host_export, c->cur.start, c->cur.length, 64);
                        c_advance(c);
                    }
                    c_consume(c, TOK_RPAREN, "Expected ')' after @[host_export(...)]");
                }
            } else {
                /* e.g. @[inline], @[must_use], @[hot] */
                if (c_match(c, TOK_LPAREN) == VM_TRUE) {
                    while (c_check(c, TOK_RPAREN) == VM_FALSE && c_check(c, TOK_EOF) == VM_FALSE) c_advance(c);
                    c_match(c, TOK_RPAREN);
                }
            }
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
        c_match(c, TOK_RBRACKET);
    }
}
