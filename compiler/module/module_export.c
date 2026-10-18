#include "module.h"
#include "../decl/decl.h"

static Token parse_export_target(Compiler *c) {
    Token ns = c->cur;
    if (c->cur.type == TOK_IDENT || (c->cur.type >= TOK_TYPE_I32 && c->cur.type <= TOK_TYPE_VOID)) {
        c_advance(c);
    } else {
        c_consume(c, TOK_IDENT, "Expected alias or namespace after 'as'");
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
 * Exports all methods (including overloads) and constants of a struct into `<alias_name>.*`
 * AND registers `alias_name` as an available module namespace so 1 file can export
 * multiple virtual modules (e.g., `export Math as std.math` + `export Random as std.random`)!
 */
void c_export_struct_namespace(Compiler *c, const char *struct_name, vm_u32 slen,
                               const char *alias_name, vm_u32 alen) {
    if (alen > 0) {
        c_mark_module_loaded(c, alias_name, alen);
    }

    char sprefix[32];
    vm_u32 sp = 0;
    for (vm_u32 i = 0; i < slen && sp < 28; i++) sprefix[sp++] = struct_name[i];
    sprefix[sp++] = '.';
    sprefix[sp] = '\0';

    vm_u32 mod_plen = vm_str_len(c->module_prefix);

    for (vm_u32 f = 0; f < c->chunk->num_functions; f++) {
        if (c->chunk->functions[f].is_pub == VM_FALSE) continue;
        const char *fname = c->chunk->functions[f].name;
        vm_u32 flen = vm_str_len(fname);
        if (flen > sp && vm_str_eq(fname, sp, sprefix, sp) == VM_TRUE) {
            const char *mname = fname + sp;
            vm_u32 mlen = flen - sp;
            if (alen > 0) {
                char alias_qname[64];
                vm_str_join_dot(alias_qname, 64, alias_name, alen, mname, mlen);
                c_register_alias(c, alias_qname, (vm_i32)f);
            }
            if (mod_plen > 0 && (alen == 0 || vm_str_eq(alias_name, alen, c->module_prefix, mod_plen - 1) == VM_TRUE)) {
                char mod_qname[64];
                vm_str_join_dot(mod_qname, 64, c->module_prefix, mod_plen, mname, mlen);
                c_register_alias(c, mod_qname, (vm_i32)f);
            }
        }
    }

    /* Natively bound stdlib intrinsics live in `$name` functions under an alias `Struct.m` */
    int alias_snap = c->num_func_aliases;
    for (int ai = 0; ai < alias_snap; ai++) {
        const char *fname = c->func_aliases[ai].alias_name;
        vm_u32 flen = vm_str_len(fname);
        int f = c->func_aliases[ai].func_id;
        if (c->chunk->functions[f].name[0] != '$' || c->chunk->functions[f].is_pub == VM_FALSE) continue;
        if (flen > sp && vm_str_eq(fname, sp, sprefix, sp) == VM_TRUE) {
            const char *mname = fname + sp;
            vm_u32 mlen = flen - sp;
            if (alen > 0) {
                char alias_qname[64];
                vm_str_join_dot(alias_qname, 64, alias_name, alen, mname, mlen);
                c_register_alias(c, alias_qname, (vm_i32)f);
            }
        }
    }

    int const_snap = c->num_global_consts;
    for (int i = 0; i < const_snap; i++) {
        const char *cname = c->global_consts[i].name;
        vm_u32 clen = c->global_consts[i].length;
        if (clen > sp && vm_str_eq(cname, sp, sprefix, sp) == VM_TRUE) {
            const char *mname = cname + sp;
            vm_u32 mlen = clen - sp;
            if (alen > 0) {
                GlobalConst *gc = &c->global_consts[c->num_global_consts++];
                vm_str_join_dot(gc->name, 64, alias_name, alen, mname, mlen);
                gc->length = vm_str_len(gc->name);
                gc->type = c->global_consts[i].type;
                gc->val = c->global_consts[i].val;
            }
            if (mod_plen > 0 && (alen == 0 || vm_str_eq(alias_name, alen, c->module_prefix, mod_plen - 1) == VM_TRUE)) {
                GlobalConst *gc = &c->global_consts[c->num_global_consts++];
                vm_str_join_dot(gc->name, 64, c->module_prefix, mod_plen, mname, mlen);
                gc->length = vm_str_len(gc->name);
                gc->type = c->global_consts[i].type;
                gc->val = c->global_consts[i].val;
            }
        }
    }
}

void c_export_single_symbol(Compiler *c, Token orig, Token exported, vm_bool has_explicit_as) {
    int sid = c_resolve_struct(c, orig.start, orig.length);
    if (sid >= 0) {
        if (has_explicit_as == VM_TRUE) {
            c_export_struct_namespace(c, orig.start, orig.length, exported.start, exported.length);
        } else if (c->module_prefix[0] != '\0') {
            vm_u32 mplen = vm_str_len(c->module_prefix);
            c_export_struct_namespace(c, orig.start, orig.length, c->module_prefix, mplen - 1);
        } else {
            c_export_struct_namespace(c, orig.start, orig.length, exported.start, exported.length);
        }
        return;
    }

    char src_name[64];
    vm_u32 mod_plen = vm_str_len(c->module_prefix);
    if (mod_plen > 0) {
        vm_str_join_dot(src_name, 64, c->module_prefix, mod_plen, orig.start, orig.length);
    } else {
        vm_str_copy(src_name, orig.start, orig.length, 64);
    }

    int cid = c_resolve_global_const(c, src_name, vm_str_len(src_name));
    if (cid < 0) cid = c_resolve_global_const(c, orig.start, orig.length);
    if (cid >= 0) {
        GlobalConst *gc = &c->global_consts[c->num_global_consts++];
        vm_str_copy(gc->name, exported.start, exported.length, 64);
        gc->length = exported.length;
        gc->type = c->global_consts[cid].type;
        gc->val = c->global_consts[cid].val;
        if (mod_plen > 0) {
            GlobalConst *mgc = &c->global_consts[c->num_global_consts++];
            vm_str_join_dot(mgc->name, 64, c->module_prefix, mod_plen, orig.start, orig.length);
            mgc->length = vm_str_len(mgc->name);
            mgc->type = c->global_consts[cid].type;
            mgc->val = c->global_consts[cid].val;
        }
        return;
    }

    if (c_resolve_func(c, src_name, vm_str_len(src_name)) < 0) {
        vm_str_copy(src_name, orig.start, orig.length, 64);
    }

    char dst_direct[64];
    vm_str_copy(dst_direct, exported.start, exported.length, 64);
    c_bind_all_overloads(c, src_name, dst_direct);

    if (mod_plen > 0) {
        char dst_mod[64];
        vm_str_join_dot(dst_mod, 64, c->module_prefix, mod_plen, exported.start, exported.length);
        c_bind_all_overloads(c, src_name, dst_mod);
    }
}

void c_compile_export(Compiler *c) {
    c_match(c, TOK_PUB);
    if (c_match(c, TOK_CONST) == VM_TRUE || c_match(c, TOK_LET) == VM_TRUE) {
        c_compile_const_decl(c, c->module_prefix, VM_TRUE);
        return;
    }
    if (c_match(c, TOK_FUNC) == VM_TRUE) {
        c_compile_func_or_method(c, c->module_prefix, VM_NULL, VM_TRUE);
        return;
    }
    if (c_match(c, TOK_OPERATOR) == VM_TRUE) {
        c_compile_top_level_operator(c);
        return;
    }
    if (c_match(c, TOK_TYPE) == VM_TRUE) {
        c_compile_type_decl(c, VM_TRUE);
        return;
    }
    if (c_match(c, TOK_STRUCT) == VM_TRUE) {
        c_compile_struct_or_type_decl(c, VM_TRUE);
        return;
    }
    if (c_match(c, TOK_ENUM) == VM_TRUE) {
        c_compile_enum_decl(c, VM_TRUE);
        return;
    }

    if (c_match(c, TOK_LBRACE) == VM_TRUE) {
        if (c_check(c, TOK_RBRACE) == VM_FALSE) {
            do {
                Token first = c->cur;
                c_consume(c, TOK_IDENT, "Expected symbol name in export '{...}'");
                Token orig = first, exported = first;
                vm_bool has_as = VM_FALSE;
                if (c_match(c, TOK_AS) == VM_TRUE) {
                    exported = parse_export_target(c);
                    has_as = VM_TRUE;
                } else if (c_match(c, TOK_COLON) == VM_TRUE || c_match(c, TOK_ASSIGN) == VM_TRUE) {
                    exported = first;
                    orig = c->cur;
                    c_consume(c, TOK_IDENT, "Expected target symbol after ':' or '='");
                    has_as = VM_TRUE;
                }
                c_export_single_symbol(c, orig, exported, has_as);
            } while (c_match(c, TOK_COMMA) == VM_TRUE);
        }
        c_consume(c, TOK_RBRACE, "Expected '}' after export block");
        c_match(c, TOK_SEMICOLON);
        return;
    }

    Token orig = c->cur;
    c_consume(c, TOK_IDENT, "Expected declaration or identifier after 'export'");
    Token exported = orig;
    vm_bool has_as = VM_FALSE;
    if (c_match(c, TOK_AS) == VM_TRUE) {
        exported = parse_export_target(c);
        has_as = VM_TRUE;
    }
    c_export_single_symbol(c, orig, exported, has_as);
    c_match(c, TOK_SEMICOLON);
}
