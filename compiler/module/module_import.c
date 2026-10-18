#include "module.h"
#include "std_sources.h"

extern int  open(const char *path, int flags, ...);
extern long read(int fd, void *buf, unsigned long count);
extern int  close(int fd);

/* Embedded std file by key ("json/parse"), or -1 */
static int find_std_key(const char *key, vm_u32 klen) {
    for (vm_u32 i = 0; i < vm_std_num_sources; i++) {
        if (vm_str_eq_cstr(key, klen, vm_std_sources[i].name) == VM_TRUE) return (int)i;
    }
    return -1;
}

/* std module `name`: std/name.vml, else std/name/mod.vml */
static int find_std_module(const char *name, vm_u32 len) {
    int i = find_std_key(name, len);
    if (i >= 0 || len > 100) return i;
    char key[128];
    vm_str_copy(key, name, len, 128);
    vm_str_copy(key + len, "/mod", 4, 128 - len);
    return find_std_key(key, len + 4);
}

static void dir_of(char *out, const char *path);
static void normalize_path(char *out, const char *dir, const char *rel, vm_u32 rlen);

/* Reads a whole file into memory owned by the chunk allocator (kept for the whole compile:
   tokens point into it). Returns VM_NULL when the file cannot be read. */
static char *read_source_file(Compiler *c, int fd) {
    vm_u32 cap = 8192, len = 0;
    char *buf = (char *)c->chunk->realloc_fn(c->chunk->alloc_ud, VM_NULL, 0, cap);
    while (buf != VM_NULL) {
        if (len + 1 >= cap) {
            char *nb = (char *)c->chunk->realloc_fn(c->chunk->alloc_ud, buf, cap, cap * 2);
            if (nb == VM_NULL) break;
            buf = nb;
            cap *= 2;
        }
        long n = read(fd, buf + len, cap - 1 - len);
        if (n <= 0) {
            buf[len] = '\0';
            return buf;
        }
        len += (vm_u32)n;
    }
    return VM_NULL;
}

vm_bool c_is_module_loaded(Compiler *c, const char *mod_name, vm_u32 len) {
    for (int i = 0; i < c->num_loaded_modules; i++) {
        vm_u32 mlen = vm_str_len(c->loaded_modules[i]);
        if (vm_str_eq(c->loaded_modules[i], mlen, mod_name, len) == VM_TRUE) {
            return VM_TRUE;
        }
    }
    return VM_FALSE;
}

void c_mark_module_loaded(Compiler *c, const char *mod_name, vm_u32 len) {
    if (c_is_module_loaded(c, mod_name, len) == VM_TRUE) return;
    if (c->num_loaded_modules < 32) {
        vm_str_copy(c->loaded_full[c->num_loaded_modules], mod_name, len, 128);
        vm_str_copy(c->loaded_modules[c->num_loaded_modules++], mod_name, len, 64);
    }
}

/* Verifies that the Host VM Configuration permits importing this module capability */
vm_bool c_check_module_feature_allowed(Compiler *c, const char *mod, vm_u32 modlen) {
    if (c->config == VM_NULL) return VM_TRUE;
    if (vm_str_eq_cstr(mod, modlen, "io") == VM_TRUE &&
        vm_config_has(c->config, VM_FEAT_STD_IO) == VM_FALSE) {
        c_error(c, "Feature 'std.io' is disabled by host VM configuration");
        return VM_FALSE;
    }
    if (vm_str_eq_cstr(mod, modlen, "coroutine") == VM_TRUE &&
        vm_config_has(c->config, VM_FEAT_COROUTINE) == VM_FALSE) {
        c_error(c, "Feature 'std.coroutine' is disabled by host VM configuration");
        return VM_FALSE;
    }
    if (vm_str_eq_cstr(mod, modlen, "random") == VM_TRUE &&
        vm_config_has(c->config, VM_FEAT_STD_RANDOM) == VM_FALSE) {
        c_error(c, "Feature 'std.random' is disabled by host VM configuration");
        return VM_FALSE;
    }
    if (vm_str_eq_cstr(mod, modlen, "env") == VM_TRUE &&
        vm_config_has(c->config, VM_FEAT_STD_ENV) == VM_FALSE) {
        c_error(c, "Feature 'std.env' is disabled by host VM configuration");
        return VM_FALSE;
    }
    if (vm_str_eq_cstr(mod, modlen, "fs") == VM_TRUE &&
        vm_config_has(c->config, VM_FEAT_STD_FS) == VM_FALSE) {
        c_error(c, "Feature 'std.fs' is disabled by host VM configuration");
        return VM_FALSE;
    }
    if (vm_str_eq_cstr(mod, modlen, "math") == VM_TRUE &&
        vm_config_has(c->config, VM_FEAT_STD_MATH) == VM_FALSE) {
        c_error(c, "Feature 'std.math' is disabled by host VM configuration");
        return VM_FALSE;
    }
    return VM_TRUE;
}

static void build_mod_path(char *out, const char *base, const char *pkg, vm_u32 pkglen, const char *mod, vm_u32 modlen) {
    /* out holds 256 bytes; parts that do not fit are cut (the open then fails with a clear error) */
    vm_u32 pos = 0;
    if (base != VM_NULL && base[0] != '\0') {
        vm_u32 blen = vm_str_len(base);
        for (vm_u32 i = 0; i < blen && pos < 200; i++) out[pos++] = base[i];
        if (pos > 0 && out[pos - 1] != '/') out[pos++] = '/';
    }
    if (pkg != VM_NULL && pkglen > 0) {
        for (vm_u32 i = 0; i < pkglen && pos < 225; i++) out[pos++] = pkg[i];
        out[pos++] = '/';
    }
    for (vm_u32 i = 0; i < modlen && pos < 248; i++) out[pos++] = mod[i];
    out[pos++] = '.'; out[pos++] = 'v'; out[pos++] = 'm'; out[pos++] = 'l';
    out[pos] = '\0';
}

/* Opens base/pkg/mod.vml, else base/pkg/mod/mod.vml (a module that is a folder); path gets the one tried last */
static int open_module_file(char *path, const char *base, const char *pkg, vm_u32 pkglen, const char *mod, vm_u32 modlen) {
    build_mod_path(path, base, pkg, pkglen, mod, modlen);
    int fd = open(path, 0);
    if (fd >= 0) return fd;
    vm_u32 n = vm_str_len(path);
    if (n < 4 || n + 5 >= 256) return -1;
    path[n - 4] = '\0';                       /* drop ".vml" */
    vm_str_copy(path + n - 4, "/mod.vml", 8, 256 - (n - 4));
    return open(path, 0);
}

void c_sync_native_stdlib(Compiler *c, vm_u32 prev_consts, vm_u32 prev_structs, vm_u32 prev_funcs) {
    for (vm_u32 k = prev_consts; k < c->chunk->num_mod_consts; k++) {
        VMModuleConst *mc = &c->chunk->mod_consts[k];
        GlobalConst *gc = &c->global_consts[c->num_global_consts++];
        vm_str_copy(gc->name, mc->name, vm_str_len(mc->name), 64);
        gc->length = vm_str_len(gc->name);
        gc->type = mc->type;
        gc->val = mc->val;
    }

    for (vm_u32 s = prev_structs; s < c->chunk->num_mod_structs; s++) {
        VMModuleStruct *ms = &c->chunk->mod_structs[s];
        vm_u32 slen = vm_str_len(ms->name);
        int sid = c_resolve_struct(c, ms->name, slen);
        if (sid < 0) {
            sid = c->num_structs++;
            StructDef *sd = &c->structs[sid];
            vm_str_copy(sd->name, ms->name, slen, 64);
            sd->owner_type_id = -1;
            sd->parent_struct_id = -1;
            sd->num_parents = 0;
            sd->num_implemented_types = 0;
            sd->num_fields = ms->num_fields;
            sd->is_generic = VM_FALSE;
            sd->num_type_params = 0;
            sd->is_optional = VM_FALSE;
            sd->is_enum = VM_FALSE;
            sd->is_result = VM_FALSE;
            for (vm_u8 f = 0; f < ms->num_fields; f++) {
                vm_str_copy(sd->field_names[f], ms->field_names[f], vm_str_len(ms->field_names[f]), 32);
                sd->field_types[f] = ms->field_types[f];
                sd->field_is_pub[f] = VM_TRUE;
                sd->field_has_default[f] = ms->field_has_default[f];
                sd->field_defaults[f] = ms->field_defaults[f];
            }
            if (sd->num_fields == 0) {
                sd->num_fields = 1;
                sd->field_names[0][0] = '\0';
                sd->field_types[0] = VM_TYPE_I32;
                sd->field_is_pub[0] = VM_FALSE;
                sd->field_has_default[0] = VM_FALSE;
            }
        }

        char sprefix[32];
        vm_u32 sp = 0;
        for (vm_u32 i = 0; i < slen && sp < 28; i++) sprefix[sp++] = ms->name[i];
        sprefix[sp++] = '.';
        sprefix[sp] = '\0';

        for (vm_u32 f = prev_funcs; f < c->chunk->num_functions; f++) {
            VMFunctionInfo *fn = &c->chunk->functions[f];
            vm_u32 flen = vm_str_len(fn->name);
            if (flen > sp && vm_str_eq(fn->name, sp, sprefix, sp) == VM_TRUE) {
                fn->owner_struct_id = (vm_i16)sid;
                for (vm_u8 p = 0; p < fn->arity && p < 8; p++) {
                    if (fn->param_struct_ids[p] == -2) {
                        fn->param_struct_ids[p] = (vm_i16)sid;
                    }
                }
                if (fn->return_struct_id == -2) {
                    fn->return_struct_id = (vm_i16)sid;
                    fn->return_reg_count = c->structs[sid].num_fields;
                    for (vm_u8 rf = 0; rf < c->structs[sid].num_fields; rf++) {
                        fn->return_elem_types[rf] = c->structs[sid].field_types[rf];
                    }
                }
            }
        }

        c_export_struct_namespace(c, ms->name, slen, ms->module_alias, vm_str_len(ms->module_alias));
    }
}

/*
 * 3-Tier Module Resolver:
 *   Tier 0: Custom Mounted Package Root (`--pkg myStd=/path` or `vm_config_mount_package`)
 *   Tier 1: Native VM Standard Library Builder (`vm_embed_load_stdlib`)
 *   Tier 2: Project-relative `<pkg>/<mod>.vml` or `--lib-path`
 */
/* Index of a loaded module by full name, or -1 */
static int find_loaded_full(Compiler *c, const char *full, vm_u32 flen) {
    for (int i = 0; i < c->num_loaded_modules; i++) {
        if (vm_str_eq(c->loaded_full[i], vm_str_len(c->loaded_full[i]), full, flen) == VM_TRUE) return i;
    }
    return -1;
}

/* `pkg.mod` as written in the import (one segment: just `mod`) */
/* `pkg.mod` as written in the import; mod may hold deeper segments as a path ("ui/widgets") */
static vm_u32 import_full_name(char *out, const char *pkg, vm_u32 pkglen, const char *mod, vm_u32 modlen) {
    if (pkg == mod && pkglen == modlen) {
        vm_str_copy(out, mod, modlen, 128);
        return vm_str_len(out);
    }
    vm_str_join_dot(out, 128, pkg, pkglen, mod, modlen);
    for (char *q = out; *q; q++) if (*q == '/') *q = '.';
    return vm_str_len(out);
}

int c_load_module_file(Compiler *c, const char *pkg, vm_u32 pkglen, const char *mod, vm_u32 modlen,
                       const char *handle, vm_u32 hlen) {
    char full[128];
    vm_u32 flen = import_full_name(full, pkg, pkglen, mod, modlen);
    int known = find_loaded_full(c, full, flen);
    if (known < 0 && pkg == mod && pkglen == modlen) {
        char sfull[128];
        vm_str_join_dot(sfull, 128, "std", 3, mod, modlen);
        for (char *q = sfull; *q; q++) if (*q == '/') *q = '.';
        known = find_loaded_full(c, sfull, vm_str_len(sfull));
    }
    if (known >= 0) return known;
    for (int i = 0; i < c->num_loaded_modules; i++) {
        if (vm_str_eq(c->loaded_modules[i], vm_str_len(c->loaded_modules[i]), handle, hlen) == VM_TRUE) {
            char msg[200];
            char hb[64];
            vm_str_copy(hb, handle, hlen, 64);
            vm_u32 n = 0;
            const char *parts[] = {"Module name '", hb, "' is already imported from ", c->loaded_full[i],
                                   "; give this import its own name with 'import ", full, " as <name>'"};
            for (int k = 0; k < 7; k++) {
                vm_u32 pl = vm_str_len(parts[k]);
                for (vm_u32 j = 0; j < pl && n + 1 < sizeof(msg); j++) msg[n++] = parts[k][j];
            }
            msg[n] = '\0';
            c_error(c, msg);
            return -1;
        }
    }
    if (c->num_loaded_modules >= 32) {
        c_error(c, "Too many imported modules (limit 32)");
        return -1;
    }
    int idx = c->num_loaded_modules++;
    vm_str_copy(c->loaded_modules[idx], handle, hlen, 64);
    vm_str_copy(c->loaded_full[idx], full, flen, 128);

    char path[256];
    int fd = -1;
    const char *src_to_compile = VM_NULL;

    /* Host modules (vml_module_new): functions are already registered as `<mod>.<name>` */
    for (vm_u32 i = 0; i < c->chunk->num_host_modules; i++) {
        if (vm_str_eq_cstr(mod, modlen, c->chunk->host_modules[i]) == VM_TRUE) {
            /* A host module may also carry builder-generated declarations (compiled below) */
            vm_bool has_src = VM_FALSE;
            for (vm_u32 k = 0; k < c->chunk->num_src_modules; k++) {
                if (vm_str_eq_cstr(mod, modlen, c->chunk->src_modules[k].name) == VM_TRUE) has_src = VM_TRUE;
            }
            if (!has_src) return idx;
        }
    }
    /* Embedded source modules (vml_module_source) */
    for (vm_u32 i = 0; i < c->chunk->num_src_modules; i++) {
        if (vm_str_eq_cstr(mod, modlen, c->chunk->src_modules[i].name) == VM_TRUE) {
            src_to_compile = c->chunk->src_modules[i].source;
            vm_str_copy(path, mod, modlen, 120);
            break;
        }
    }

    /* Standard library written in `.vml` (embedded at build time; AST compiler only) */
    vm_bool is_std = VM_FALSE;
    if (src_to_compile == VM_NULL && c->ast_compile_module != VM_NULL &&
        (vm_str_eq_cstr(pkg, pkglen, "std") == VM_TRUE || pkglen == modlen)) {
        int si = find_std_module(mod, modlen);
        if (si >= 0) {
            src_to_compile = vm_std_sources[si].source;
            vm_u32 kl = vm_str_len(vm_std_sources[si].name);
            vm_str_copy(path, "std/", 4, 250);
            vm_str_copy(path + 4, vm_std_sources[si].name, kl, 240);
            vm_u32 pl = 4 + kl;
            vm_str_copy(path + pl, ".vml", 4, 250 - pl);
            is_std = VM_TRUE;
            if (pkg == mod && pkglen == modlen) vm_str_join_dot(c->loaded_full[idx], 128, "std", 3, mod, modlen);
        }
    }

    /* Tier 0: Custom Mounted Package Root (`--pkg myStd=/path` or `vm_config_mount_package`) */
    const char *mounted_dir = vm_config_resolve_package(c->config, pkg, pkglen);
    if (mounted_dir != VM_NULL) {
        fd = open_module_file(path, mounted_dir, VM_NULL, 0, mod, modlen);
    }

    /* Tier 1: Native VM Standard Library Builder (`vm_embed_load_stdlib`) */
    if (fd < 0 && src_to_compile == VM_NULL) {
        vm_u32 prev_consts = c->chunk->num_mod_consts;
        vm_u32 prev_structs = c->chunk->num_mod_structs;
        vm_u32 prev_funcs = c->chunk->num_functions;
        if (vm_embed_load_stdlib(c->chunk, pkg, pkglen, mod, modlen) == VM_TRUE) {
            c_sync_native_stdlib(c, prev_consts, prev_structs, prev_funcs);
            return idx;
        }
    }

    /* Tier 2: Project-relative `<pkg>/<mod>.vml` or `--lib-path` */
    if (fd < 0 && src_to_compile == VM_NULL) {
        fd = open_module_file(path, VM_NULL, pkg, pkglen, mod, modlen);
        if (fd < 0 && c->config != VM_NULL && c->config->lib_path[0] != '\0') {
            fd = open_module_file(path, c->config->lib_path, pkg, pkglen, mod, modlen);
            if (fd < 0) fd = open_module_file(path, c->config->lib_path, VM_NULL, 0, mod, modlen);
        }
    }

    if (src_to_compile != VM_NULL) {
        /* embedded source: nothing to read */
    } else if (fd >= 0) {
        src_to_compile = read_source_file(c, fd);
        close(fd);
        if (src_to_compile == VM_NULL) {
            c_error(c, "Out of memory while reading an imported module");
            return -1;
        }
    } else {
        c_error(c, "Could not resolve imported module in Native VM Stdlib, local path, or --lib-path");
        return -1;
    }

    Lexer saved_lex = c->lex;
    Token saved_cur = c->cur;
    vm_u8 saved_file_id = c->chunk->current_file_id;
    vm_u16 saved_line = c->chunk->current_line;
    char  saved_prefix[32];
    vm_str_copy(saved_prefix, c->module_prefix, vm_str_len(c->module_prefix), 32);

    (void)vm_chunk_set_file(c->chunk, path);

    vm_u32 ppos = 0;
    for (vm_u32 i = 0; i < hlen && ppos < 28; i++) c->module_prefix[ppos++] = handle[i];
    c->module_prefix[ppos++] = '.';
    c->module_prefix[ppos] = '\0';
    int saved_module = c->cur_module_idx;
    c->cur_module_idx = idx;
    /* Relative imports inside the module start at its file and stay inside its folder */
    char saved_file[C_PATH_CAP], saved_root[C_PATH_CAP], mdir[C_PATH_CAP];
    vm_str_copy(saved_file, c->cur_file, vm_str_len(c->cur_file), C_PATH_CAP);
    vm_str_copy(saved_root, c->pkg_root, vm_str_len(c->pkg_root), C_PATH_CAP);
    vm_str_copy(c->cur_file, path, vm_str_len(path), C_PATH_CAP);
    dir_of(mdir, path);
    normalize_path(c->pkg_root, mdir, "", 0);

    vm_bool saved_std = c->in_std_module;
    c->in_std_module = is_std;
    if (c->ast_compile_module != VM_NULL) {
        c->ast_compile_module(c->ast_gen, src_to_compile, path);
    } else {
        c_compile_unit_source(c, src_to_compile);
    }

    c->in_std_module = saved_std;
    c->cur_module_idx = saved_module;
    vm_str_copy(c->cur_file, saved_file, vm_str_len(saved_file), C_PATH_CAP);
    vm_str_copy(c->pkg_root, saved_root, vm_str_len(saved_root), C_PATH_CAP);
    vm_str_copy(c->module_prefix, saved_prefix, vm_str_len(saved_prefix), 32);
    c->chunk->current_file_id = saved_file_id;
    c->chunk->current_line = saved_line;
    c->lex = saved_lex;
    c->cur = saved_cur;
    return idx;
}

static vm_bool is_symbol_in_list(const Token *list, int count, const char *sym, vm_u32 symlen) {
    for (int i = 0; i < count; i++) {
        if (vm_str_eq(list[i].start, list[i].length, sym, symlen) == VM_TRUE) {
            return VM_TRUE;
        }
    }
    return VM_FALSE;
}

void c_bind_wildcard_symbols(Compiler *c, Token mod_tok, const char *dst_prefix, vm_u32 dst_plen,
                             const Token *exclude_list, int num_exclude) {
    char mod_prefix[32];
    vm_u32 mp = 0;
    for (vm_u32 i = 0; i < mod_tok.length && mp < 28; i++) mod_prefix[mp++] = mod_tok.start[i];
    mod_prefix[mp++] = '.';
    mod_prefix[mp] = '\0';

    vm_u32 fn_snap = c->chunk->num_functions;
    for (vm_u32 f = 0; f < fn_snap; f++) {
        const char *fname = c->chunk->functions[f].name;
        vm_u32 flen = vm_str_len(fname);
        if (flen > mp && vm_str_eq(fname, mp, mod_prefix, mp) == VM_TRUE) {
            const char *sym = fname + mp;
            vm_u32 symlen = flen - mp;
            if (is_symbol_in_list(exclude_list, num_exclude, sym, symlen) == VM_TRUE) continue;
            char aliased_qname[64];
            if (dst_plen > 0) {
                vm_str_join_dot(aliased_qname, 64, dst_prefix, dst_plen, sym, symlen);
            } else {
                vm_str_copy(aliased_qname, sym, symlen, 64);
            }
            c_register_alias(c, aliased_qname, (vm_i32)f);
        }
    }

    int asnap = c->num_func_aliases;
    for (int a = 0; a < asnap; a++) {
        const char *aname = c->func_aliases[a].alias_name;
        vm_u32 alen = vm_str_len(aname);
        if (alen > mp && vm_str_eq(aname, mp, mod_prefix, mp) == VM_TRUE) {
            const char *sym = aname + mp;
            vm_u32 symlen = alen - mp;
            if (is_symbol_in_list(exclude_list, num_exclude, sym, symlen) == VM_TRUE) continue;
            char aliased_qname[64];
            if (dst_plen > 0) {
                vm_str_join_dot(aliased_qname, 64, dst_prefix, dst_plen, sym, symlen);
            } else {
                vm_str_copy(aliased_qname, sym, symlen, 64);
            }
            c_register_alias(c, aliased_qname, c->func_aliases[a].func_id);
        }
    }

    int csnap = c->num_global_consts;
    for (int g = 0; g < csnap; g++) {
        const char *cname = c->global_consts[g].name;
        vm_u32 clen = c->global_consts[g].length;
        if (clen > mp && vm_str_eq(cname, mp, mod_prefix, mp) == VM_TRUE) {
            const char *sym = cname + mp;
            vm_u32 symlen = clen - mp;
            if (is_symbol_in_list(exclude_list, num_exclude, sym, symlen) == VM_TRUE) continue;
            GlobalConst *gc = &c->global_consts[c->num_global_consts++];
            if (dst_plen > 0) {
                vm_str_join_dot(gc->name, 64, dst_prefix, dst_plen, sym, symlen);
            } else {
                vm_str_copy(gc->name, sym, symlen, 64);
            }
            gc->length = vm_str_len(gc->name);
            gc->type = c->global_consts[g].type;
            gc->val = c->global_consts[g].val;
        }
    }
}

void c_compile_import(Compiler *c) {
    Token pkg_tok = c->cur;
    c_consume(c, TOK_IDENT, "Expected package/module name after 'import'");

    Token mod_tok = pkg_tok;
    if (c_match(c, TOK_DOT) == VM_TRUE) {
        mod_tok = c->cur;
        if (c->cur.type == TOK_IDENT || (c->cur.type >= TOK_TYPE_I32 && c->cur.type <= TOK_TYPE_VOID)) {
            c_advance(c);
        } else {
            c_consume(c, TOK_IDENT, "Expected module name after '.'");
        }
    }

    if (c_check_module_feature_allowed(c, mod_tok.start, mod_tok.length) == VM_FALSE) {
        return;
    }

    (void)c_load_module_file(c, pkg_tok.start, pkg_tok.length, mod_tok.start, mod_tok.length, mod_tok.start, mod_tok.length);

    Token   sel_orig[16], sel_alias[16];
    int     num_sel = 0;
    vm_bool has_braces = VM_FALSE;
    vm_bool has_wildcard = VM_FALSE;

    if (c_match(c, TOK_LBRACE) == VM_TRUE) {
        has_braces = VM_TRUE;
        if (c_check(c, TOK_RBRACE) == VM_FALSE) {
            do {
                if (c_match(c, TOK_STAR) == VM_TRUE) {
                    has_wildcard = VM_TRUE;
                    continue;
                }
                Token orig = c->cur;
                c_consume(c, TOK_IDENT, "Expected symbol name or '*' in import '{...}'");
                Token alias = orig;
                if (c_match(c, TOK_AS) == VM_TRUE) {
                    alias = c->cur;
                    c_consume(c, TOK_IDENT, "Expected alias name after 'as'");
                }
                sel_orig[num_sel] = orig;
                sel_alias[num_sel] = alias;
                num_sel++;
            } while (c_match(c, TOK_COMMA) == VM_TRUE);
        }
        c_consume(c, TOK_RBRACE, "Expected '}' after selective import list");
    }

    vm_bool has_ns_alias = c_match(c, TOK_AS);
    Token   ns_alias = {0};
    if (has_ns_alias == VM_TRUE) {
        ns_alias = c->cur;
        c_consume(c, TOK_IDENT, "Expected module alias name after 'as'");
    }

    if (has_braces == VM_TRUE) {
        const char *prefix = has_ns_alias ? ns_alias.start : "";
        vm_u32 plen = has_ns_alias ? ns_alias.length : 0;

        for (int s = 0; s < num_sel; s++) {
            char target_name[64], bound_name[64];
            vm_str_join_dot(target_name, 64, mod_tok.start, mod_tok.length, sel_orig[s].start, sel_orig[s].length);
            if (plen > 0) {
                vm_str_join_dot(bound_name, 64, prefix, plen, sel_alias[s].start, sel_alias[s].length);
            } else {
                vm_str_copy(bound_name, sel_alias[s].start, sel_alias[s].length, 64);
            }
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
                c_error(c, "Imported symbol not found in module");
            }
        }

        if (has_wildcard == VM_TRUE) {
            c_bind_wildcard_symbols(c, mod_tok, prefix, plen, sel_orig, num_sel);
        }
    } else if (has_ns_alias == VM_TRUE) {
        c_bind_wildcard_symbols(c, mod_tok, ns_alias.start, ns_alias.length, VM_NULL, 0);
    }

    c_match(c, TOK_SEMICOLON);
}

/* ---------------------------------------------------------------------------
 * import "./path"
 * ------------------------------------------------------------------------- */

static void msg_join(char *out, vm_u32 cap, const char *const *parts, int n) {
    vm_u32 k = 0;
    for (int i = 0; i < n; i++) {
        if (parts[i] == VM_NULL) continue;
        vm_u32 pl = vm_str_len(parts[i]);
        for (vm_u32 j = 0; j < pl && k + 1 < cap; j++) out[k++] = parts[i][j];
    }
    out[k] = '\0';
}

/* Folder part of a path ("a/b/c.vml" -> "a/b", "c.vml" -> ".") */
static void dir_of(char *out, const char *path) {
    vm_u32 n = vm_str_len(path), cut = 0;
    vm_bool found = VM_FALSE;
    for (vm_u32 i = 0; i < n; i++) if (path[i] == '/') { cut = i; found = VM_TRUE; }
    if (!found) { vm_str_copy(out, ".", 1, C_PATH_CAP); return; }
    if (cut == 0) cut = 1;   /* "/x" -> "/" */
    vm_str_copy(out, path, cut, C_PATH_CAP);
}

/* Joins dir + rel and folds "." / ".." segments. Leading ".." that cannot fold are kept. */
static void normalize_path(char *out, const char *dir, const char *rel, vm_u32 rlen) {
    char tmp[2 * C_PATH_CAP];
    vm_u32 n = 0, dl = vm_str_len(dir);
    for (vm_u32 i = 0; i < dl && n < C_PATH_CAP; i++) tmp[n++] = dir[i];
    tmp[n++] = '/';
    for (vm_u32 i = 0; i < rlen && n + 2 < sizeof(tmp); i++) tmp[n++] = rel[i];
    tmp[n] = '\0';
    vm_bool absolute = vm_bool_from_int(tmp[0] == '/');
    const char *seg[64];
    vm_u32 seglen[64];
    int ns = 0;
    vm_u32 i = 0;
    while (i < n) {
        while (i < n && tmp[i] == '/') i++;
        vm_u32 st = i;
        while (i < n && tmp[i] != '/') i++;
        vm_u32 sl = i - st;
        if (sl == 0 || (sl == 1 && tmp[st] == '.')) continue;
        if (sl == 2 && tmp[st] == '.' && tmp[st + 1] == '.' && ns > 0 &&
            !(seglen[ns - 1] == 2 && seg[ns - 1][0] == '.' && seg[ns - 1][1] == '.')) {
            ns--;
            continue;
        }
        if (ns < 64) { seg[ns] = &tmp[st]; seglen[ns] = sl; ns++; }
    }
    vm_u32 k = 0;
    if (absolute) out[k++] = '/';
    for (int s = 0; s < ns; s++) {
        if (s > 0 && k < C_PATH_CAP - 2) out[k++] = '/';
        for (vm_u32 j = 0; j < seglen[s] && k < C_PATH_CAP - 2; j++) out[k++] = seg[s][j];
    }
    if (k == 0) out[k++] = '.';
    out[k] = '\0';
}

void c_set_main_file(Compiler *c, const char *path) {
    c->num_path_imported = 0;
    vm_str_copy(c->cur_file, path, vm_str_len(path), C_PATH_CAP);
    char d[C_PATH_CAP];
    dir_of(d, path);
    normalize_path(c->pkg_root, d, "", 0);
}

/* True when `path` is `root` or lies inside it (both normalized) */
static vm_bool inside_root(const char *root, const char *path) {
    vm_u32 rl = vm_str_len(root), pl = vm_str_len(path);
    if (vm_str_eq_cstr(root, rl, ".") == VM_TRUE) {
        return vm_bool_from_int(path[0] != '/' && !(pl >= 2 && path[0] == '.' && path[1] == '.' && (pl == 2 || path[2] == '/')));
    }
    if (pl < rl || vm_str_eq(path, rl, root, rl) == VM_FALSE) return VM_FALSE;
    return vm_bool_from_int(pl == rl || path[rl] == '/');
}

/* Name in a `module a.b` line at the top of `src` ("" when there is none) */
static void peek_module_name(const char *src, char *out) {
    Lexer lx;
    lexer_init(&lx, src);
    out[0] = '\0';
    Token t = lexer_next(&lx);
    if (t.type != TOK_IDENT || vm_str_eq_cstr(t.start, t.length, "module") == VM_FALSE) return;
    vm_u32 k = 0;
    for (;;) {
        Token nt = lexer_next(&lx);
        if (nt.type != TOK_IDENT) break;
        if (k > 0 && k < 126) out[k++] = '.';
        for (vm_u32 j = 0; j < nt.length && k < 126; j++) out[k++] = nt.start[j];
        Token dot = lexer_peek(&lx);
        if (dot.type != TOK_DOT) break;
        (void)lexer_next(&lx);
    }
    out[k] = '\0';
}

static vm_u32 root_seg_len(const char *name) {
    vm_u32 n = 0;
    while (name[n] != '\0' && name[n] != '.') n++;
    return n;
}

int c_import_path(Compiler *c, const char *rel, vm_u32 rlen, vm_bool has_select,
                  const char *alias, vm_u32 alen, vm_bool *is_internal) {
    char shown[128], msg[300];
    vm_str_copy(shown, rel, rlen, 128);
    *is_internal = VM_FALSE;
    vm_bool dot_rel = vm_bool_from_int((rlen >= 2 && rel[0] == '.' && rel[1] == '/') ||
                                       (rlen >= 3 && rel[0] == '.' && rel[1] == '.' && rel[2] == '/'));
    if (!dot_rel) {
        const char *p[] = {"Import path '", shown, "' must start with ./ or ../ (paths are relative to this file)"};
        msg_join(msg, sizeof(msg), p, 3);
        c_error(c, msg);
        return -1;
    }
    char dir[C_PATH_CAP], path[C_PATH_CAP];
    dir_of(dir, c->cur_file);
    normalize_path(path, dir, rel, rlen);
    if (inside_root(c->pkg_root, path) == VM_FALSE) {
        const char *p[] = {"Import path '", shown, "' points outside the package (", c->pkg_root, ")"};
        msg_join(msg, sizeof(msg), p, 5);
        c_error(c, msg);
        return -1;
    }
    /* Extension: none -> .vml; .vmb / .vmi come with the build cache (SPEC 6) */
    vm_u32 pl = vm_str_len(path);
    int dot = -1;
    for (vm_u32 i = 0; i < pl; i++) {
        if (path[i] == '/') dot = -1;
        else if (path[i] == '.') dot = (int)i;
    }
    if (dot < 0) {
        vm_str_copy(path + pl, ".vml", 4, C_PATH_CAP - pl);
    } else if (vm_str_eq_cstr(path + dot, pl - (vm_u32)dot, ".vml") == VM_FALSE) {
        const char *p[] = {"Import '", shown, "': only .vml files can be imported by path for now (.vmb / .vmi come with the build cache)"};
        msg_join(msg, sizeof(msg), p, 3);
        c_error(c, msg);
        return -1;
    }

    for (int i = 0; i < c->num_path_imported; i++) {
        if (vm_str_eq_cstr(path, vm_str_len(path), c->path_imported[i]) == VM_TRUE) {
            /* Already loaded: internal files are done; a module is found again by its name below */
            break;
        }
    }

    const char *src = VM_NULL;
    vm_bool from_std = VM_FALSE;
    if (c->in_std_module && vm_str_len(path) > 8 && vm_str_eq_cstr(path, 4, "std/") == VM_TRUE) {
        /* Inside std: files come from the tree built into the binary, never from disk */
        int si = find_std_key(path + 4, vm_str_len(path) - 8);
        if (si >= 0) {
            src = vm_std_sources[si].source;
            from_std = VM_TRUE;
        }
    } else {
        int fd = open(path, 0);
        if (fd >= 0) {
            src = read_source_file(c, fd);
            close(fd);
            if (src == VM_NULL) {
                c_error(c, "Out of memory while reading an imported file");
                return -1;
            }
        }
    }
    if (src == VM_NULL) {
        const char *p[] = {"Import cannot find ", shown, " (looked for ", path, ")"};
        msg_join(msg, sizeof(msg), p, 5);
        c_error(c, msg);
        return -1;
    }

    char declared[128];
    peek_module_name(src, declared);
    const char *mine = (c->cur_module_idx >= 0) ? c->loaded_full[c->cur_module_idx] : "";
    vm_bool internal = vm_bool_from_int(declared[0] == '\0' ||
                                        (mine[0] != '\0' && vm_str_eq_cstr(declared, vm_str_len(declared), mine) == VM_TRUE));

    if (internal) {
        *is_internal = VM_TRUE;
        if (has_select || alen > 0) {
            const char *p[] = {"Files that are part of this module cannot use {...} or 'as' ('", shown,
                               "' has no 'module' line, so it joins the module that imports it)"};
            msg_join(msg, sizeof(msg), p, 3);
            c_error(c, msg);
            return -1;
        }
        for (int i = 0; i < c->num_path_imported; i++) {
            if (vm_str_eq_cstr(path, vm_str_len(path), c->path_imported[i]) == VM_TRUE) return -1;
        }
        if (c->num_path_imported < 64) vm_str_copy(c->path_imported[c->num_path_imported++], path, vm_str_len(path), C_PATH_CAP);
        char saved_file[C_PATH_CAP];
        vm_str_copy(saved_file, c->cur_file, vm_str_len(c->cur_file), C_PATH_CAP);
        vm_u8 saved_file_id = c->chunk->current_file_id;
        vm_u16 saved_line = c->chunk->current_line;
        Lexer saved_lex = c->lex;
        Token saved_cur = c->cur;
        vm_str_copy(c->cur_file, path, vm_str_len(path), C_PATH_CAP);
        (void)vm_chunk_set_file(c->chunk, path);
        if (c->ast_compile_module != VM_NULL) c->ast_compile_module(c->ast_gen, src, path);
        else c_error(c, "import \"./path\" needs the AST compiler");
        vm_str_copy(c->cur_file, saved_file, vm_str_len(saved_file), C_PATH_CAP);
        c->chunk->current_file_id = saved_file_id;
        c->chunk->current_line = saved_line;
        c->lex = saved_lex;
        c->cur = saved_cur;
        return -1;
    }

    /* Another module: same package root as the importer (the main program may use any non-std name) */
    if (mine[0] != '\0') {
        vm_u32 dr = root_seg_len(declared), mr = root_seg_len(mine);
        if (vm_str_eq(declared, dr, mine, mr) == VM_FALSE) {
            char root[64];
            vm_str_copy(root, mine, mr, 64);
            const char *p[] = {"Module name '", declared, "' must be under '", root, "', the package it was loaded from"};
            msg_join(msg, sizeof(msg), p, 5);
            c_error(c, msg);
            return -1;
        }
    }
    int known = find_loaded_full(c, declared, vm_str_len(declared));
    if (known >= 0) return known;

    const char *handle = declared;
    vm_u32 hlen = vm_str_len(declared);
    for (vm_u32 i = 0; i < vm_str_len(declared); i++) {
        if (declared[i] == '.') { handle = declared + i + 1; hlen = vm_str_len(declared) - i - 1; }
    }
    if (alen > 0 && !has_select) { handle = alias; hlen = alen; }
    for (int i = 0; i < c->num_loaded_modules; i++) {
        if (vm_str_eq(c->loaded_modules[i], vm_str_len(c->loaded_modules[i]), handle, hlen) == VM_TRUE) {
            char hb[64];
            vm_str_copy(hb, handle, hlen, 64);
            const char *p[] = {"Module name '", hb, "' is already imported from ", c->loaded_full[i],
                               "; give this import its own name with 'import \"", shown, "\" as <name>'"};
            msg_join(msg, sizeof(msg), p, 7);
            c_error(c, msg);
            return -1;
        }
    }
    if (c->num_loaded_modules >= 32) {
        c_error(c, "Too many imported modules (limit 32)");
        return -1;
    }
    int idx = c->num_loaded_modules++;
    vm_str_copy(c->loaded_modules[idx], handle, hlen, 64);
    vm_str_copy(c->loaded_full[idx], declared, vm_str_len(declared), 128);
    if (c->num_path_imported < 64) vm_str_copy(c->path_imported[c->num_path_imported++], path, vm_str_len(path), C_PATH_CAP);

    char saved_file[C_PATH_CAP], saved_prefix[32];
    vm_str_copy(saved_file, c->cur_file, vm_str_len(c->cur_file), C_PATH_CAP);
    vm_str_copy(saved_prefix, c->module_prefix, vm_str_len(c->module_prefix), 32);
    vm_u8 saved_file_id = c->chunk->current_file_id;
    vm_u16 saved_line = c->chunk->current_line;
    Lexer saved_lex = c->lex;
    Token saved_cur = c->cur;
    int saved_module = c->cur_module_idx;
    vm_bool saved_std = c->in_std_module;

    vm_u32 pp = 0;
    for (vm_u32 i = 0; i < hlen && pp < 28; i++) c->module_prefix[pp++] = handle[i];
    c->module_prefix[pp++] = '.';
    c->module_prefix[pp] = '\0';
    c->cur_module_idx = idx;
    c->in_std_module = from_std;
    vm_str_copy(c->cur_file, path, vm_str_len(path), C_PATH_CAP);
    (void)vm_chunk_set_file(c->chunk, path);
    if (c->ast_compile_module != VM_NULL) c->ast_compile_module(c->ast_gen, src, path);
    else c_error(c, "import \"./path\" needs the AST compiler");

    c->in_std_module = saved_std;
    c->cur_module_idx = saved_module;
    vm_str_copy(c->module_prefix, saved_prefix, vm_str_len(saved_prefix), 32);
    vm_str_copy(c->cur_file, saved_file, vm_str_len(saved_file), C_PATH_CAP);
    c->chunk->current_file_id = saved_file_id;
    c->chunk->current_line = saved_line;
    c->lex = saved_lex;
    c->cur = saved_cur;
    return idx;
}
