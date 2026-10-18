#include "../vm/opt/vm_fuse.h"
#include "compiler.h"
#include "gen/gen.h"
#include "scope/context.h"
#include "module/module.h"
#include "decl/decl.h"
#include "../vm/stdlib/vm_stdlib.h"

void c_compile_unit_source(Compiler *c, const char *src) {
    lexer_init(&c->lex, src);
    c_advance(c);

    while (c_check(c, TOK_EOF) == VM_FALSE && c->had_error == VM_FALSE) {
        c_parse_attributes(c);
        if (c_match(c, TOK_IMPORT) == VM_TRUE) {
            c_compile_import(c);
        } else if (c_match(c, TOK_EXPORT) == VM_TRUE || c_match(c, TOK_PUB) == VM_TRUE) {
            c_compile_export(c);
        } else if (c_match(c, TOK_ENUM) == VM_TRUE) {
            c_compile_enum_decl(c, VM_FALSE);
        } else if (c_match(c, TOK_TYPE) == VM_TRUE) {
            c_compile_type_decl(c, VM_FALSE);
        } else if (c_match(c, TOK_STRUCT) == VM_TRUE) {
            c_compile_struct_or_type_decl(c, VM_FALSE);
        } else if (c_match(c, TOK_FUNC) == VM_TRUE) {
            c_compile_func_or_method(c, c->module_prefix, VM_NULL, VM_TRUE);
        } else if (c_match(c, TOK_OPERATOR) == VM_TRUE) {
            c_compile_top_level_operator(c);
        } else if (c_match(c, TOK_CONST) == VM_TRUE) {
            c_compile_const_decl(c, c->module_prefix, VM_FALSE);
        } else {
            c_error(c, "Unexpected top-level token");
            break;
        }
    }
}

vm_bool vm_compile_source_with_config(const char *source, VMChunk *chunk, const VMConfig *cfg) {
    return gen_compile_source(source, chunk, cfg);
}

vm_bool vm_compile_source_legacy(const char *source, VMChunk *chunk, const VMConfig *cfg) {
    Compiler c;
    c.chunk = chunk; c.config = cfg;
    c.num_locals = 0; c.num_outer_locals = 0; c.scope_depth = 0; c.next_reg = 0; c.max_reg = 0;
    c.num_global_consts = 0; c.num_func_aliases = 0; c.num_loaded_modules = 0; c.cur_module_idx = -1;
    for (int fi = 0; fi < VM_MAX_FUNCTIONS; fi++) c.func_module[fi] = -1;
    c.num_path_imported = 0; c.cur_file[0] = '\0'; c.pkg_root[0] = '\0';
    c_func_index_reset(&c);
    c.num_structs = 0; c.current_struct_id = -1; c.num_type_contracts = 0;
    c.num_enum_variants = 0; c.num_generics = 0; c.num_active_tparams = 0;
    c.module_prefix[0] = '\0'; c.num_breaks = 0; c.num_continues = 0;
    c.num_defers = 0;
    c.pending_deprecated = VM_FALSE;
    c.pending_deprecated_msg[0] = '\0';
    c.pending_host_export[0] = '\0';
    c.pending_intrinsic[0] = '\0';
    c.in_std_module = VM_FALSE;
    c.current_ret_type = VM_TYPE_VOID;
    c.current_ret_struct_id = -1; c.current_func_id = -1; c.had_error = VM_FALSE;
    c.last_parsed_elem_type = VM_TYPE_VOID; c.last_empty_array_ip = 0xFFFFFFFFu;
    c.ast_gen = VM_NULL; c.ast_compile_module = VM_NULL;

    /* Auto-register Native Primitive Types (`String`, `Array`, `Map`) from `vm/stdlib/` */
    vm_u32 p_consts = chunk->num_mod_consts;
    vm_u32 p_structs = chunk->num_mod_structs;
    vm_u32 p_funcs = chunk->num_functions;
    vm_stdlib_register_prelude(chunk);
    c_sync_native_stdlib(&c, p_consts, p_structs, p_funcs);

    c_compile_unit_source(&c, source);
    if (c.had_error == VM_FALSE) {
        c_verify_all_type_contracts(&c);
    }
    if (c.had_error == VM_TRUE) return VM_FALSE;
    if (chunk->entry_func_id < 0) {
        vm_sys_write_cstr("[Compile Error] Missing entry point: please define 'func main()'\n");
        return VM_FALSE;
    }
    vm_fuse_superinstructions(chunk, 0, chunk->code_count);
    return VM_TRUE;
}

vm_bool vm_compile_source(const char *source, VMChunk *chunk) {
    return vm_compile_source_with_config(source, chunk, VM_NULL);
}
