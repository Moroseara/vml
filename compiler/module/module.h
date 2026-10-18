#ifndef COMPILER_MODULE_H
#define COMPILER_MODULE_H

#include "../scope/context.h"

void    c_compile_unit_source(Compiler *c, const char *src);

/* `module_import.c` */
void    c_sync_native_stdlib(Compiler *c, vm_u32 prev_consts, vm_u32 prev_structs, vm_u32 prev_funcs);
vm_bool c_is_module_loaded(Compiler *c, const char *mod_name, vm_u32 len);
void    c_mark_module_loaded(Compiler *c, const char *mod_name, vm_u32 len);
void    c_compile_import(Compiler *c);
vm_bool c_check_module_feature_allowed(Compiler *c, const char *mod, vm_u32 modlen);
/* Records the main program's file: relative imports resolve from it and stay inside its folder */
void    c_set_main_file(Compiler *c, const char *path);
/* `import "./x"`: a file with no `module` line (or this module's name) joins the current module and
   returns -1; a file naming another module is loaded as that module and its index is returned.
   `select_or_alias` = the import used {...} or `as` (not allowed for internal files). */
int     c_import_path(Compiler *c, const char *rel, vm_u32 rlen, vm_bool has_select,
                      const char *alias, vm_u32 alen, vm_bool *is_internal);
/* Loads `pkg.mod` (once) under `handle`, the name code uses for it; returns its index or -1 */
int     c_load_module_file(Compiler *c, const char *pkg, vm_u32 pkglen, const char *mod, vm_u32 modlen,
                           const char *handle, vm_u32 hlen);
void    c_bind_wildcard_symbols(Compiler *c, Token mod_tok, const char *dst_prefix, vm_u32 dst_plen,
                                const Token *exclude_list, int num_exclude);

/* `module_export.c` */
void    c_export_struct_namespace(Compiler *c, const char *struct_name, vm_u32 slen,
                                  const char *alias_name, vm_u32 alen);
void    c_compile_export(Compiler *c);
void    c_export_single_symbol(Compiler *c, Token orig, Token exported, vm_bool has_explicit_as);

#endif /* COMPILER_MODULE_H */
