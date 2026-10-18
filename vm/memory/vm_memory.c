#include "vm_memory.h"
#include "../syscall/vm_syscall.h"

const char *vm_type_name(VMDataType type) {
    switch (type) {
        case VM_TYPE_VOID:   return "void";
        case VM_TYPE_I32:    return "i32";
        case VM_TYPE_I64:    return "i64";
        case VM_TYPE_F32:    return "float";
        case VM_TYPE_F64:    return "f64";
        case VM_TYPE_BOOL:   return "bool";
        case VM_TYPE_STRING: return "string";
        case VM_TYPE_ARRAY:  return "array";
        case VM_TYPE_MAP:    return "map";
        default:             return "unknown";
    }
}

const char *vm_opcode_name(VMOpCode op) {
    switch (op) {
        case OP_HALT:         return "HALT";
        case OP_LOADK:        return "LOADK";
        case OP_LOAD_IMM_I32: return "LOAD_IMM_I32";
        case OP_LOAD_BOOL:    return "LOAD_BOOL";
        case OP_MOVE:         return "MOVE";
        case OP_ADD_I32:      return "ADD_I32";
        case OP_SUB_I32:      return "SUB_I32";
        case OP_MUL_I32:      return "MUL_I32";
        case OP_DIV_I32:      return "DIV_I32";
        case OP_MOD_I32:      return "MOD_I32";
        case OP_ADD_IMM_I32:  return "ADD_IMM_I32";
        case OP_BAND_I32:     return "BAND_I32";
        case OP_BOR_I32:      return "BOR_I32";
        case OP_BXOR_I32:     return "BXOR_I32";
        case OP_BNOT_I32:     return "BNOT_I32";
        case OP_SHL_I32:      return "SHL_I32";
        case OP_SHR_I32:      return "SHR_I32";
        case OP_ADD_I64:      return "ADD_I64";
        case OP_SUB_I64:      return "SUB_I64";
        case OP_MUL_I64:      return "MUL_I64";
        case OP_DIV_I64:      return "DIV_I64";
        case OP_ADD_F32:      return "ADD_F32";
        case OP_SUB_F32:      return "SUB_F32";
        case OP_MUL_F32:      return "MUL_F32";
        case OP_DIV_F32:      return "DIV_F32";
        case OP_ADD_F64:      return "ADD_F64";
        case OP_SUB_F64:      return "SUB_F64";
        case OP_MUL_F64:      return "MUL_F64";
        case OP_DIV_F64:      return "DIV_F64";
        case OP_EQ_I32:       return "EQ_I32";
        case OP_NE_I32:       return "NE_I32";
        case OP_LT_I32:       return "LT_I32";
        case OP_LE_I32:       return "LE_LE_I32";
        case OP_GT_I32:       return "GT_I32";
        case OP_GE_I32:       return "GE_I32";
        case OP_EQ_F32:       return "EQ_F32";
        case OP_NE_F32:       return "NE_F32";
        case OP_LT_F32:       return "LT_F32";
        case OP_LE_F32:       return "LE_F32";
        case OP_GT_F32:       return "GT_F32";
        case OP_GE_F32:       return "GE_F32";
        case OP_EQ_I64:       return "EQ_I64";
        case OP_NE_I64:       return "NE_I64";
        case OP_LT_I64:       return "LT_I64";
        case OP_LE_I64:       return "LE_I64";
        case OP_GT_I64:       return "GT_I64";
        case OP_GE_I64:       return "GE_I64";
        case OP_EQ_F64:       return "EQ_F64";
        case OP_NE_F64:       return "NE_F64";
        case OP_LT_F64:       return "LT_F64";
        case OP_LE_F64:       return "LE_F64";
        case OP_GT_F64:       return "GT_F64";
        case OP_GE_F64:       return "GE_F64";
        case OP_EQ_STR:       return "EQ_STR";
        case OP_NE_STR:       return "NE_STR";
        case OP_LT_STR:       return "LT_STR";
        case OP_LE_STR:       return "LE_STR";
        case OP_GT_STR:       return "GT_STR";
        case OP_GE_STR:       return "GE_STR";
        case OP_AND_BOOL:     return "AND_BOOL";
        case OP_OR_BOOL:      return "OR_BOOL";
        case OP_NOT_BOOL:     return "NOT_BOOL";
        case OP_JMP:          return "JMP";
        case OP_JMP_FALSE:    return "JMP_FALSE";
        case OP_JMP_TRUE:     return "JMP_TRUE";
        case OP_JMP_NZ_I32:   return "JMP_NZ_I32";
        case OP_FORLOOP_I32:  return "FORLOOP_I32";
        case OP_CALL:         return "CALL";
        case OP_CALL_REG:     return "CALL_REG";
        case OP_RET:          return "RET";
        case OP_GET_OUTER:    return "GET_OUTER";
        case OP_CLOSURE:      return "CLOSURE";
        case OP_CELL_NEW:     return "CELL_NEW";
        case OP_CELL_GET:     return "CELL_GET";
        case OP_CELL_SET:     return "CELL_SET";
        case OP_UPVAL_GET:    return "UPVAL_GET";
        case OP_UPVAL_SET:    return "UPVAL_SET";
        case OP_TRAP:         return "TRAP";
        case OP_TRY_BEGIN:    return "TRY_BEGIN";
        case OP_TRY_END:      return "TRY_END";
        case OP_STRLEN:       return "STRLEN";
        case OP_STRCAT:       return "STRCAT";
        case OP_STRUPPER:     return "STRUPPER";
        case OP_STRLOWER:     return "STRLOWER";
        case OP_STRSUB:       return "STRSUB";
        case OP_STRFIND:      return "STRFIND";
        case OP_STRTRIM:      return "STRTRIM";
        case OP_STRREPLACE:   return "STRREPLACE";
        case OP_STRFORMAT:    return "STRFORMAT";
        case OP_MATH:         return "MATH";
        case OP_CAST:         return "CAST";
        case OP_PRINT_I32:    return "PRINT_I32";
        case OP_PRINT_I64:    return "PRINT_I64";
        case OP_PRINT_F32:    return "PRINT_F32";
        case OP_PRINT_F64:    return "PRINT_F64";
        case OP_PRINT_BOOL:   return "PRINT_BOOL";
        case OP_PRINT_STR:    return "PRINT_STR";
        case OP_ARR_NEW:      return "ARR_NEW";
        case OP_ARR_LEN:      return "ARR_LEN";
        case OP_ARR_GET:      return "ARR_GET";
        case OP_ARR_SET:      return "ARR_SET";
        case OP_ARR_PUSH:     return "ARR_PUSH";
        case OP_ARR_POP:      return "ARR_POP";
        case OP_MAP_NEW:      return "MAP_NEW";
        case OP_MAP_LEN:      return "MAP_LEN";
        case OP_MAP_GET:      return "MAP_GET";
        case OP_MAP_SET:      return "MAP_SET";
        case OP_MAP_HAS:      return "MAP_HAS";
        case OP_MAP_DEL:      return "MAP_DEL";
        case OP_MAP_KEY_AT:   return "MAP_KEY_AT";
        case OP_MAP_VAL_AT:   return "MAP_VAL_AT";
        case OP_EQ_I32_JF: return "EQ_I32_JF";
        case OP_NE_I32_JF: return "NE_I32_JF";
        case OP_LT_I32_JF: return "LT_I32_JF";
        case OP_LE_I32_JF: return "LE_I32_JF";
        case OP_GT_I32_JF: return "GT_I32_JF";
        case OP_GE_I32_JF: return "GE_I32_JF";
        case OP_EQ_F64_JF: return "EQ_F64_JF";
        case OP_NE_F64_JF: return "NE_F64_JF";
        case OP_LT_F64_JF: return "LT_F64_JF";
        case OP_LE_F64_JF: return "LE_F64_JF";
        case OP_GT_F64_JF: return "GT_F64_JF";
        case OP_GE_F64_JF: return "GE_F64_JF";
        case OP_LI_EQ_I32_JF: return "LI_EQ_I32_JF";
        case OP_LI_NE_I32_JF: return "LI_NE_I32_JF";
        case OP_LI_LT_I32_JF: return "LI_LT_I32_JF";
        case OP_LI_LE_I32_JF: return "LI_LE_I32_JF";
        case OP_LI_GT_I32_JF: return "LI_GT_I32_JF";
        case OP_LI_GE_I32_JF: return "LI_GE_I32_JF";
        case OP_LK_EQ_I32_JF: return "LK_EQ_I32_JF";
        case OP_LK_NE_I32_JF: return "LK_NE_I32_JF";
        case OP_LK_LT_I32_JF: return "LK_LT_I32_JF";
        case OP_LK_LE_I32_JF: return "LK_LE_I32_JF";
        case OP_LK_GT_I32_JF: return "LK_GT_I32_JF";
        case OP_LK_GE_I32_JF: return "LK_GE_I32_JF";
        case OP_LI_SUB_I32: return "LI_SUB_I32";
        case OP_LI_MUL_I32: return "LI_MUL_I32";
        case OP_LI_DIV_I32: return "LI_DIV_I32";
        case OP_LI_MOD_I32: return "LI_MOD_I32";
        case OP_ADD_I32_MV: return "ADD_I32_MV";
        case OP_SUB_I32_MV: return "SUB_I32_MV";
        case OP_MUL_I32_MV: return "MUL_I32_MV";
        case OP_MOD_I32_MV: return "MOD_I32_MV";
        case OP_ADD_IMM_I32_MV: return "ADD_IMM_I32_MV";
        case OP_ADD_I64_MV: return "ADD_I64_MV";
        case OP_ADD_F32_MV: return "ADD_F32_MV";
        case OP_SUB_F32_MV: return "SUB_F32_MV";
        case OP_MUL_F32_MV: return "MUL_F32_MV";
        case OP_ADD_F64_MV: return "ADD_F64_MV";
        case OP_SUB_F64_MV: return "SUB_F64_MV";
        case OP_MUL_F64_MV: return "MUL_F64_MV";
        case OP_STRCAT_MV: return "STRCAT_MV";
        case OP_MOD_I64: return "MOD_I64";
        case OP_ADD_IMM_I64: return "ADD_IMM_I64";
        case OP_LOAD_IMM_I64: return "LOAD_IMM_I64";
        case OP_BAND_I64: return "BAND_I64";
        case OP_BOR_I64: return "BOR_I64";
        case OP_BXOR_I64: return "BXOR_I64";
        case OP_BNOT_I64: return "BNOT_I64";
        case OP_SHL_I64: return "SHL_I64";
        case OP_SHR_I64: return "SHR_I64";
        case OP_I32_TO_I64: return "I32_TO_I64";
        case OP_I64_TO_I32: return "I64_TO_I32";
        case OP_JMP_NZ_I64: return "JMP_NZ_I64";
        case OP_FORLOOP_I64: return "FORLOOP_I64";
        case OP_EQ_I64_JF: return "EQ_I64_JF";
        case OP_NE_I64_JF: return "NE_I64_JF";
        case OP_LT_I64_JF: return "LT_I64_JF";
        case OP_LE_I64_JF: return "LE_I64_JF";
        case OP_GT_I64_JF: return "GT_I64_JF";
        case OP_GE_I64_JF: return "GE_I64_JF";
        case OP_LI_EQ_I64_JF: return "LI_EQ_I64_JF";
        case OP_LI_NE_I64_JF: return "LI_NE_I64_JF";
        case OP_LI_LT_I64_JF: return "LI_LT_I64_JF";
        case OP_LI_LE_I64_JF: return "LI_LE_I64_JF";
        case OP_LI_GT_I64_JF: return "LI_GT_I64_JF";
        case OP_LI_GE_I64_JF: return "LI_GE_I64_JF";
        case OP_ADD_IMM_I64_MV: return "ADD_IMM_I64_MV";
        case OP_SUB_I64_MV: return "SUB_I64_MV";
        default:              return "UNKNOWN";
    }
}

void vm_chunk_init(VMChunk *chunk) {
    vm_chunk_init_with_alloc(chunk, vm_sys_default_realloc, VM_NULL);
}

void vm_chunk_init_with_alloc(VMChunk *chunk, VMReallocFn fn, void *user_data) {
    chunk->realloc_fn = (fn != VM_NULL) ? fn : vm_sys_default_realloc;
    chunk->alloc_ud = user_data;
    chunk->out_of_memory = VM_FALSE;
    chunk->interface_src = VM_NULL;
    chunk->interface_len = 0;
    chunk->strings = VM_NULL;
    chunk->string_hashes = VM_NULL;
    chunk->strings_cap = 0;
    chunk->string_arena = VM_NULL;
    chunk->string_arena_cap = 0;
    chunk->intern_slots = VM_NULL;
    chunk->intern_cap = 0;
    chunk->intern_used = 0;
    chunk->arrays = VM_NULL;
    chunk->arrays_cap = 0;
    chunk->maps = VM_NULL;
    chunk->maps_cap = 0;
    chunk->cells = VM_NULL;
    chunk->cell_types = VM_NULL;
    chunk->num_cells = chunk->cells_cap = 0;
    chunk->closures = VM_NULL;
    chunk->num_closures = chunk->closures_cap = 0;
    chunk->closure_cells = VM_NULL;
    chunk->num_closure_cells = chunk->closure_cells_cap = 0;
    chunk->string_dead = VM_NULL;
    VMFreeList empty_list = {VM_NULL, 0, 0};
    chunk->free_strings = chunk->free_arrays = chunk->free_maps = chunk->free_cells = chunk->free_closures = empty_list;
    chunk->sealed_strings = 0;
    chunk->sealed_arena = 0;
    chunk->gc_sealed = VM_FALSE;
    chunk->gc_pending = VM_FALSE;
    chunk->gc_disabled = VM_FALSE;
    chunk->gc_allocated = 0;
    chunk->gc_threshold = VM_GC_MIN_THRESHOLD;
    chunk->gc_collections = 0;
    chunk->gc_num_cpus = 0;
    chunk->host_state = VM_NULL;
    chunk->calling_native_fid = -1;
    chunk->native_error = VM_FALSE;
    chunk->prog_args = VM_NULL;
    chunk->prog_argc = 0;
    chunk->exit_requested = VM_FALSE;
    chunk->exit_code = 0;
    chunk->native_error_msg[0] = '\0';
    chunk->require_entry = VM_TRUE;
    chunk->num_host_modules = 0;
    chunk->num_host_types = 0;
    chunk->num_src_modules = 0;
    chunk->code_count = 0;
    chunk->num_files = 0;
    chunk->current_file_id = 0;
    chunk->current_line = 1;
    (void)vm_chunk_set_file(chunk, "<script>");
    chunk->num_constants = 0;
    chunk->num_strings = 0;
    chunk->string_arena_used = 0;
    chunk->num_arrays = 0;
    chunk->num_maps = 0;
    chunk->num_tasks = 0;
    chunk->num_functions = 0;
    chunk->entry_func_id = -1;
    chunk->num_mod_consts = 0;
    chunk->num_mod_structs = 0;
    chunk->last_error_str_id = vm_chunk_add_string(chunk, "", 0);
    chunk->last_traceback_str_id = chunk->last_error_str_id;
    chunk->active_env_map_id = -1;
    chunk->active_func_id = -1;
    for (vm_u32 i = 0; i < VM_MAX_FUNCTIONS; i++) {
        chunk->functions[i].native_fn = VM_NULL;
        chunk->functions[i].coerce_args_to_string = VM_FALSE;
        chunk->functions[i].return_func_type_id = -1;
        chunk->functions[i].env_map_id = -1;
        chunk->functions[i].enclosing_func_id = -1;
        chunk->functions[i].num_upvals = 0;
        chunk->functions[i].host_fn = VM_NULL;
        chunk->functions[i].host_ud = VM_NULL;
        for (int p = 0; p < VM_MAX_PARAMS; p++) chunk->functions[i].param_func_type[p] = -1;
    }
    for (int t = 0; t < VM_TYPE_COUNT; t++) {
        chunk->primitive_struct_name[t][0] = '\0';
    }
}

vm_u32 vm_lib_begin_func(VMChunk *chunk, const char *qname, vm_u8 arity, vm_u8 min_arity,
                         const VMDataType *param_types, VMDataType ret_type) {
    if (chunk->num_functions >= VM_MAX_FUNCTIONS) return VM_MAX_FUNCTIONS - 1;
    vm_u32 fid = chunk->num_functions++;
    VMFunctionInfo *fn = &chunk->functions[fid];
    vm_str_copy(fn->name, qname, vm_str_len(qname), 64);
    fn->start_ip = chunk->code_count;
    fn->arity = arity;
    fn->min_arity = min_arity;
    fn->num_regs = arity;
    fn->is_pub = VM_TRUE;
    fn->is_deprecated = VM_FALSE;
    fn->coerce_args_to_string = VM_FALSE;
    fn->deprecated_msg[0] = '\0';
    fn->owner_struct_id = -1;
    for (vm_u8 i = 0; i < arity && i < VM_MAX_PARAMS; i++) {
        fn->param_types[i] = (param_types != VM_NULL) ? param_types[i] : VM_TYPE_I32;
        fn->param_struct_ids[i] = -1;
        fn->param_has_default[i] = VM_TRUE ? VM_FALSE : VM_FALSE;
        fn->param_defaults[i] = (VMReg){0};
    }
    fn->return_type = ret_type;
    fn->return_elem_types[0] = ret_type;
    fn->return_struct_id = -1;
    fn->return_func_type_id = -1;
    fn->return_reg_count = 1;
    fn->env_map_id = -1;
    fn->enclosing_func_id = -1;
    fn->native_fn = VM_NULL;
    return fid;
}

void vm_lib_end_func(VMChunk *chunk, vm_u32 fid, vm_u8 num_regs) {
    chunk->functions[fid].num_regs = num_regs;
}

vm_u32 vm_lib_add_native(VMChunk *chunk, const char *qname, vm_u8 arity,
                         const VMDataType *param_types, VMDataType ret_type, VMNativeFn native_fn) {
    vm_u32 fid = vm_lib_begin_func(chunk, qname, arity, arity, param_types, ret_type);
    chunk->functions[fid].native_fn = native_fn;
    return fid;
}

void vm_lib_set_coerce_string_args(VMChunk *chunk, vm_u32 fid) {
    if (fid < chunk->num_functions) {
        chunk->functions[fid].coerce_args_to_string = VM_TRUE;
    }
}

void vm_lib_add_const(VMChunk *chunk, const char *qname, VMDataType type, VMReg val) {
    if (chunk->num_mod_consts >= 64) return;
    VMModuleConst *mc = &chunk->mod_consts[chunk->num_mod_consts++];
    vm_str_copy(mc->name, qname, vm_str_len(qname), 64);
    mc->type = type;
    mc->val = val;
}

vm_u32 vm_lib_add_struct(VMChunk *chunk, const char *struct_name, const char *module_alias) {
    for (vm_u32 i = 0; i < chunk->num_mod_structs; i++) {
        if (vm_str_eq_cstr(chunk->mod_structs[i].name, vm_str_len(chunk->mod_structs[i].name), struct_name) == VM_TRUE) {
            return i;
        }
    }
    if (chunk->num_mod_structs >= 16) return 0;
    vm_u32 idx = chunk->num_mod_structs++;
    VMModuleStruct *ms = &chunk->mod_structs[idx];
    vm_str_copy(ms->name, struct_name, vm_str_len(struct_name), 64);
    if (module_alias != VM_NULL) {
        vm_str_copy(ms->module_alias, module_alias, vm_str_len(module_alias), 64);
    } else {
        ms->module_alias[0] = '\0';
    }
    ms->num_fields = 0;
    return idx;
}

void vm_lib_add_struct_field(VMChunk *chunk, vm_u32 msid, const char *fname,
                             VMDataType ftype, vm_bool has_def, VMReg def_val) {
    if (msid >= chunk->num_mod_structs) return;
    VMModuleStruct *ms = &chunk->mod_structs[msid];
    if (ms->num_fields >= 4) return;
    vm_u8 f = ms->num_fields++;
    vm_str_copy(ms->field_names[f], fname, vm_str_len(fname), 32);
    ms->field_types[f] = ftype;
    ms->field_has_default[f] = has_def;
    ms->field_defaults[f] = def_val;
}

void vm_lib_bind_primitive(VMChunk *chunk, VMDataType prim_type, const char *struct_name) {
    if (prim_type >= 0 && prim_type < VM_TYPE_COUNT) {
        vm_str_copy(chunk->primitive_struct_name[prim_type], struct_name, vm_str_len(struct_name), 32);
    }
}

vm_u8 vm_chunk_set_file(VMChunk *chunk, const char *file_path) {
    if (file_path == VM_NULL || file_path[0] == '\0') return chunk->current_file_id;
    vm_u32 plen = vm_str_len(file_path);
    for (vm_u8 i = 0; i < chunk->num_files; i++) {
        vm_u32 flen = vm_str_len(chunk->file_names[i]);
        if (vm_str_eq(chunk->file_names[i], flen, file_path, plen) == VM_TRUE) {
            chunk->current_file_id = i;
            return i;
        }
    }
    if (chunk->num_files < VM_MAX_FILES) {
        vm_u8 id = chunk->num_files++;
        vm_str_copy(chunk->file_names[id], file_path, plen, 256);
        chunk->current_file_id = id;
        return id;
    }
    return chunk->current_file_id;
}

vm_u32 vm_chunk_emit(VMChunk *chunk, VMInst inst) {
    vm_u32 idx = chunk->code_count;
    if (idx < VM_MAX_CODE_SIZE) {
        chunk->code[idx] = inst;
        chunk->code_lines[idx] = chunk->current_line;
        chunk->code_files[idx] = chunk->current_file_id;
        chunk->code_count++;
    }
    return idx;
}

vm_u32 vm_chunk_add_constant(VMChunk *chunk, VMReg val, VMDataType type) {
    for (vm_u32 i = 0; i < chunk->num_constants; i++) {
        if (chunk->constant_types[i] == type && chunk->constants[i].raw == val.raw) {
            return i;
        }
    }
    vm_u32 idx = chunk->num_constants++;
    chunk->constants[idx] = val;
    chunk->constant_types[idx] = type;
    return idx;
}

void *vm_chunk_realloc(VMChunk *chunk, void *ptr, vm_size old_size, vm_size new_size) {
    void *p = chunk->realloc_fn(chunk->alloc_ud, ptr, old_size, new_size);
    if (p == VM_NULL && new_size > 0) chunk->out_of_memory = VM_TRUE;
    return p;
}

/* Replaces the program's interface text (copied) */
void vm_chunk_set_interface(VMChunk *chunk, const char *text, vm_u32 len) {
    if (chunk->interface_src) vm_chunk_realloc(chunk, chunk->interface_src, chunk->interface_len + 1, 0);
    chunk->interface_src = VM_NULL;
    chunk->interface_len = 0;
    if (text == VM_NULL || len == 0) return;
    char *buf = (char *)vm_chunk_realloc(chunk, VM_NULL, 0, len + 1);
    if (buf == VM_NULL) return;
    for (vm_u32 i = 0; i < len; i++) buf[i] = text[i];
    buf[len] = '\0';
    chunk->interface_src = buf;
    chunk->interface_len = len;
}

void vm_chunk_free(VMChunk *chunk) {
    vm_chunk_set_interface(chunk, VM_NULL, 0);
    for (vm_u32 i = 0; i < chunk->num_arrays; i++) {
        VMArray *a = &chunk->arrays[i];
        vm_chunk_realloc(chunk, a->items, sizeof(VMReg) * a->capacity, 0);
    }
    vm_chunk_realloc(chunk, chunk->arrays, sizeof(VMArray) * chunk->arrays_cap, 0);
    for (vm_u32 i = 0; i < chunk->num_maps; i++) {
        VMMap *m = &chunk->maps[i];
        vm_chunk_realloc(chunk, m->keys, sizeof(vm_u32) * m->capacity, 0);
        vm_chunk_realloc(chunk, m->vals, sizeof(VMReg) * m->capacity, 0);
    }
    vm_chunk_realloc(chunk, chunk->maps, sizeof(VMMap) * chunk->maps_cap, 0);
    vm_chunk_realloc(chunk, chunk->cells, sizeof(VMReg) * chunk->cells_cap, 0);
    vm_chunk_realloc(chunk, chunk->cell_types, chunk->cells_cap, 0);
    chunk->cell_types = VM_NULL;
    vm_chunk_realloc(chunk, chunk->closures, sizeof(VMClosure) * chunk->closures_cap, 0);
    vm_chunk_realloc(chunk, chunk->closure_cells, sizeof(vm_u32) * chunk->closure_cells_cap, 0);
    vm_chunk_realloc(chunk, chunk->string_dead, chunk->strings_cap, 0);
    VMFreeList *lists[5] = {&chunk->free_strings, &chunk->free_arrays, &chunk->free_maps, &chunk->free_cells, &chunk->free_closures};
    for (int i = 0; i < 5; i++) {
        vm_chunk_realloc(chunk, lists[i]->ids, sizeof(vm_u32) * lists[i]->cap, 0);
        lists[i]->ids = VM_NULL;
        lists[i]->count = lists[i]->cap = 0;
    }
    chunk->string_dead = VM_NULL;
    chunk->cells = VM_NULL;
    chunk->closures = VM_NULL;
    chunk->closure_cells = VM_NULL;
    chunk->num_cells = chunk->cells_cap = 0;
    chunk->num_closures = chunk->closures_cap = 0;
    chunk->num_closure_cells = chunk->closure_cells_cap = 0;
    chunk->maps = VM_NULL;
    chunk->num_maps = chunk->maps_cap = 0;
    vm_chunk_realloc(chunk, chunk->strings, sizeof(VMString) * chunk->strings_cap, 0);
    vm_chunk_realloc(chunk, chunk->string_hashes, sizeof(vm_u32) * chunk->strings_cap, 0);
    vm_chunk_realloc(chunk, chunk->string_arena, chunk->string_arena_cap, 0);
    vm_chunk_realloc(chunk, chunk->intern_slots, sizeof(vm_u32) * chunk->intern_cap, 0);
    chunk->arrays = VM_NULL;
    chunk->strings = VM_NULL;
    chunk->string_hashes = VM_NULL;
    chunk->string_arena = VM_NULL;
    chunk->intern_slots = VM_NULL;
    chunk->num_arrays = chunk->arrays_cap = 0;
    chunk->num_strings = chunk->strings_cap = 0;
    chunk->string_arena_used = chunk->string_arena_cap = 0;
    chunk->intern_cap = chunk->intern_used = 0;
}

char *vm_chunk_arena_reserve(VMChunk *chunk, vm_u32 extra) {
    vm_u32 need = chunk->string_arena_used + extra + 1;
    if (need > chunk->string_arena_cap) {
        vm_u32 new_cap = (chunk->string_arena_cap > 0) ? chunk->string_arena_cap * 2 : VM_INIT_STRING_ARENA;
        while (new_cap < need) new_cap *= 2;
        char *old = chunk->string_arena;
        vm_u32 old_cap = chunk->string_arena_cap;
        char *arena = (char *)vm_chunk_realloc(chunk, old, old_cap, new_cap);
        if (arena == VM_NULL) return VM_NULL;
        if (arena != old && old != VM_NULL) {
            /* Arena moved: rebase every live string slice that pointed into the old block */
            vm_u64 lo = (vm_u64)old;
            vm_u64 hi = lo + old_cap;
            for (vm_u32 i = 0; i < chunk->num_strings; i++) {
                vm_u64 p = (vm_u64)chunk->strings[i].chars;
                if (p >= lo && p < hi) chunk->strings[i].chars = arena + (p - lo);
            }
        }
        chunk->string_arena = arena;
        chunk->string_arena_cap = new_cap;
    }
    return &chunk->string_arena[chunk->string_arena_used];
}

static vm_u32 str_hash(const char *chars, vm_u32 length) {
    vm_u32 h = 2166136261u; /* FNV-1a */
    for (vm_u32 i = 0; i < length; i++) {
        h ^= (vm_u8)chars[i];
        h *= 16777619u;
    }
    return h;
}

static vm_bool intern_rebuild(VMChunk *chunk) {
    /* Only interned strings live in the table; runtime temporaries never do */
    vm_u32 interned = 0;
    for (vm_u32 sid = 0; sid < chunk->num_strings; sid++) {
        if (chunk->string_dead[sid] == VM_STR_INTERNED) interned++;
    }
    vm_u32 new_cap = VM_INIT_INTERN_SLOTS;
    while (new_cap < interned * 4) new_cap *= 2;
    vm_u32 *slots = chunk->intern_slots;
    if (new_cap != chunk->intern_cap) {
        slots = (vm_u32 *)vm_chunk_realloc(chunk, chunk->intern_slots,
                                           sizeof(vm_u32) * chunk->intern_cap, sizeof(vm_u32) * new_cap);
        if (slots == VM_NULL) return VM_FALSE;
    }
    for (vm_u32 i = 0; i < new_cap; i++) slots[i] = 0;
    vm_u32 mask = new_cap - 1;
    for (vm_u32 sid = 0; sid < chunk->num_strings; sid++) {
        if (chunk->string_dead[sid] != VM_STR_INTERNED) continue;
        vm_u32 i = chunk->string_hashes[sid] & mask;
        while (slots[i] != 0) i = (i + 1) & mask;
        slots[i] = sid + 1;
    }
    chunk->intern_slots = slots;
    chunk->intern_cap = new_cap;
    chunk->intern_used = interned;
    return VM_TRUE;
}

static vm_bool ensure_string_slot(VMChunk *chunk) {
    if (chunk->num_strings < chunk->strings_cap) return VM_TRUE;
    vm_u32 new_cap = (chunk->strings_cap > 0) ? chunk->strings_cap * 2 : VM_INIT_STRING_POOL;
    VMString *strs = (VMString *)vm_chunk_realloc(chunk, chunk->strings,
                                                  sizeof(VMString) * chunk->strings_cap, sizeof(VMString) * new_cap);
    if (strs == VM_NULL) return VM_FALSE;
    chunk->strings = strs;
    vm_u32 *hashes = (vm_u32 *)vm_chunk_realloc(chunk, chunk->string_hashes,
                                                sizeof(vm_u32) * chunk->strings_cap, sizeof(vm_u32) * new_cap);
    if (hashes == VM_NULL) return VM_FALSE;
    chunk->string_hashes = hashes;
    vm_u8 *dead = (vm_u8 *)vm_chunk_realloc(chunk, chunk->string_dead, chunk->strings_cap, new_cap);
    if (dead == VM_NULL) return VM_FALSE;
    for (vm_u32 i = chunk->strings_cap; i < new_cap; i++) dead[i] = 0;
    chunk->string_dead = dead;
    chunk->strings_cap = new_cap;
    return VM_TRUE;
}

/* Next free string id: a slot freed by the GC, or a new one at the end */
static vm_u32 take_string_slot(VMChunk *chunk) {
    if (chunk->free_strings.count > 0) {
        vm_u32 id = chunk->free_strings.ids[--chunk->free_strings.count];
        chunk->string_dead[id] = 0;
        return id;
    }
    chunk->string_dead[chunk->num_strings] = 0;
    return chunk->num_strings++;
}

/* Runtime-built strings are NOT interned: probing a huge intern table for every temporary
 * costs far more than it saves. Equality and map lookup compare cached hash + bytes instead. */
static vm_u32 commit_arena_string(VMChunk *chunk, const char *dst, vm_u32 len) {
    if (ensure_string_slot(chunk) == VM_FALSE) return 0;
    chunk->string_arena_used += len + 1;
    vm_chunk_gc_account(chunk, (vm_u64)len + 1 + 24);
    vm_u32 id = take_string_slot(chunk);
    chunk->strings[id].chars = dst;
    chunk->strings[id].length = len;
    chunk->string_hashes[id] = str_hash(dst, len);
    return id;
}

/* Registers a string built in place in memory from vm_chunk_arena_reserve */
vm_u32 vm_chunk_commit_string(VMChunk *chunk, const char *dst, vm_u32 len) {
    return commit_arena_string(chunk, dst, len);
}

void vm_chunk_intern_rebuild(VMChunk *chunk) {
    (void)intern_rebuild(chunk);
}

vm_u32 vm_chunk_add_string(VMChunk *chunk, const char *chars, vm_u32 length) {
    if (ensure_string_slot(chunk) == VM_FALSE) return 0;
    if ((chunk->intern_used + 1) * 2 > chunk->intern_cap) {
        if (intern_rebuild(chunk) == VM_FALSE) return 0;
    }
    vm_u32 h = str_hash(chars, length);
    vm_u32 mask = chunk->intern_cap - 1;
    vm_u32 i = h & mask;
    vm_u32 reuse = 0xFFFFFFFFu;
    while (chunk->intern_slots[i] != 0) {
        vm_u32 sid = chunk->intern_slots[i] - 1;
        if (sid >= chunk->num_strings || chunk->string_dead[sid] != VM_STR_INTERNED) {
            /* Stale entry (string freed by the GC) */
            if (reuse == 0xFFFFFFFFu) reuse = i;
        } else if (chunk->string_hashes[sid] == h &&
                   vm_str_eq(chunk->strings[sid].chars, chunk->strings[sid].length, chars, length) == VM_TRUE) {
            return sid;
        }
        i = (i + 1) & mask;
    }

    if (ensure_string_slot(chunk) == VM_FALSE) return 0;

    vm_u32 id = take_string_slot(chunk);
    chunk->strings[id].chars = chars;
    chunk->strings[id].length = length;
    chunk->string_hashes[id] = h;
    chunk->string_dead[id] = VM_STR_INTERNED;
    if (reuse != 0xFFFFFFFFu) {
        chunk->intern_slots[reuse] = id + 1;
    } else {
        chunk->intern_slots[i] = id + 1;
        chunk->intern_used++;
    }
    return id;
}

vm_u32 vm_chunk_new_string(VMChunk *chunk, const char *chars, vm_u32 length) {
    char *dst = vm_chunk_arena_reserve(chunk, length);
    if (dst == VM_NULL) return 0;
    for (vm_u32 i = 0; i < length; i++) dst[i] = chars[i];
    dst[length] = '\0';
    return commit_arena_string(chunk, dst, length);
}

vm_u32 vm_chunk_concat_strings(VMChunk *chunk, vm_u32 sid_a, vm_u32 sid_b) {
    vm_u32 total = chunk->strings[sid_a].length + chunk->strings[sid_b].length;
    char *dst = vm_chunk_arena_reserve(chunk, total);
    if (dst == VM_NULL) return sid_a;
    VMString a = chunk->strings[sid_a];
    VMString b = chunk->strings[sid_b];
    for (vm_u32 i = 0; i < a.length; i++) dst[i] = a.chars[i];
    for (vm_u32 i = 0; i < b.length; i++) dst[a.length + i] = b.chars[i];
    dst[total] = '\0';
    return commit_arena_string(chunk, dst, total);
}

vm_u32 vm_chunk_upper(VMChunk *chunk, vm_u32 sid) {
    char *dst = vm_chunk_arena_reserve(chunk, chunk->strings[sid].length);
    if (dst == VM_NULL) return sid;
    VMString s = chunk->strings[sid];
    for (vm_u32 i = 0; i < s.length; i++) {
        char ch = s.chars[i];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - ('a' - 'A'));
        dst[i] = ch;
    }
    dst[s.length] = '\0';
    return commit_arena_string(chunk, dst, s.length);
}

vm_u32 vm_chunk_lower(VMChunk *chunk, vm_u32 sid) {
    char *dst = vm_chunk_arena_reserve(chunk, chunk->strings[sid].length);
    if (dst == VM_NULL) return sid;
    VMString s = chunk->strings[sid];
    for (vm_u32 i = 0; i < s.length; i++) {
        char ch = s.chars[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + ('a' - 'A'));
        dst[i] = ch;
    }
    dst[s.length] = '\0';
    return commit_arena_string(chunk, dst, s.length);
}

vm_u32 vm_chunk_substr(VMChunk *chunk, vm_u32 sid, vm_i32 start, vm_i32 count) {
    vm_u32 slen = chunk->strings[sid].length;
    if (start < 0) start = 0;
    if ((vm_u32)start >= slen || count <= 0) {
        return vm_chunk_add_string(chunk, "", 0);
    }
    if ((vm_u32)start + (vm_u32)count > slen) {
        count = (vm_i32)(slen - (vm_u32)start);
    }
    char *dst = vm_chunk_arena_reserve(chunk, (vm_u32)count);
    if (dst == VM_NULL) return sid;
    VMString s = chunk->strings[sid];
    for (vm_i32 i = 0; i < count; i++) {
        dst[i] = s.chars[start + i];
    }
    dst[count] = '\0';
    return commit_arena_string(chunk, dst, (vm_u32)count);
}

vm_i32 vm_chunk_str_find(const VMChunk *chunk, vm_u32 sid, vm_u32 sub_sid) {
    VMString s = chunk->strings[sid];
    VMString sub = chunk->strings[sub_sid];
    if (sub.length == 0) return 0;
    if (sub.length > s.length) return -1;
    vm_u32 limit = s.length - sub.length;
    for (vm_u32 i = 0; i <= limit; i++) {
        if (vm_str_eq(&s.chars[i], sub.length, sub.chars, sub.length) == VM_TRUE) {
            return (vm_i32)i;
        }
    }
    return -1;
}

vm_u32 vm_chunk_str_trim(VMChunk *chunk, vm_u32 sid) {
    VMString s = chunk->strings[sid];
    vm_u32 start = 0;
    vm_u32 end = s.length;
    while (start < end && (s.chars[start] == ' ' || s.chars[start] == '\t' || s.chars[start] == '\r' || s.chars[start] == '\n')) {
        start++;
    }
    while (end > start && (s.chars[end - 1] == ' ' || s.chars[end - 1] == '\t' || s.chars[end - 1] == '\r' || s.chars[end - 1] == '\n')) {
        end--;
    }
    return vm_chunk_substr(chunk, sid, (vm_i32)start, (vm_i32)(end - start));
}

vm_u32 vm_chunk_str_replace(VMChunk *chunk, vm_u32 sid, vm_u32 old_sid, vm_u32 new_sid) {
    VMString s = chunk->strings[sid];
    VMString old_s = chunk->strings[old_sid];
    VMString new_s = chunk->strings[new_sid];
    if (old_s.length == 0) return sid;

    /* Pass 1: exact output size, so the arena is reserved (and possibly moved) only once */
    vm_u32 hits = 0;
    for (vm_u32 i = 0; i + old_s.length <= s.length;) {
        if (vm_str_eq(&s.chars[i], old_s.length, old_s.chars, old_s.length) == VM_TRUE) {
            hits++;
            i += old_s.length;
        } else {
            i++;
        }
    }
    vm_u32 out_len = s.length - hits * old_s.length + hits * new_s.length;
    char *dst = vm_chunk_arena_reserve(chunk, out_len);
    if (dst == VM_NULL) return sid;
    s = chunk->strings[sid];
    old_s = chunk->strings[old_sid];
    new_s = chunk->strings[new_sid];

    vm_u32 dlen = 0;
    vm_u32 i = 0;
    while (i < s.length) {
        if (i + old_s.length <= s.length &&
            vm_str_eq(&s.chars[i], old_s.length, old_s.chars, old_s.length) == VM_TRUE) {
            for (vm_u32 k = 0; k < new_s.length; k++) dst[dlen++] = new_s.chars[k];
            i += old_s.length;
        } else {
            dst[dlen++] = s.chars[i++];
        }
    }
    dst[dlen] = '\0';
    return commit_arena_string(chunk, dst, dlen);
}

vm_u32 vm_chunk_str_format(VMChunk *chunk, vm_u32 fmt_sid, vm_u32 arg_sid) {
    /* At most one placeholder is replaced, so output <= fmt + arg */
    char *dst = vm_chunk_arena_reserve(chunk, chunk->strings[fmt_sid].length + chunk->strings[arg_sid].length);
    if (dst == VM_NULL) return fmt_sid;
    VMString fmt = chunk->strings[fmt_sid];
    VMString arg = chunk->strings[arg_sid];
    vm_u32 dlen = 0;
    vm_bool replaced = VM_FALSE;
    vm_u32 i = 0;
    while (i < fmt.length) {
        if (replaced == VM_FALSE && fmt.chars[i] == '{') {
            vm_u32 close_idx = i + 1;
            while (close_idx < fmt.length && fmt.chars[close_idx] != '}') close_idx++;
            if (close_idx < fmt.length) {
                for (vm_u32 k = 0; k < arg.length; k++) dst[dlen++] = arg.chars[k];
                i = close_idx + 1;
                replaced = VM_TRUE;
                continue;
            }
        } else if (replaced == VM_FALSE && fmt.chars[i] == '%' && i + 1 < fmt.length &&
                   (fmt.chars[i + 1] == 's' || fmt.chars[i + 1] == 'd' || fmt.chars[i + 1] == 'f' || fmt.chars[i + 1] == 'v')) {
            for (vm_u32 k = 0; k < arg.length; k++) dst[dlen++] = arg.chars[k];
            i += 2;
            replaced = VM_TRUE;
            continue;
        }
        dst[dlen++] = fmt.chars[i++];
    }
    dst[dlen] = '\0';
    return commit_arena_string(chunk, dst, dlen);
}

vm_i32 vm_chunk_str_cmp(const VMChunk *chunk, vm_u32 sid_a, vm_u32 sid_b) {
    if (sid_a == sid_b) return 0;
    VMString a = chunk->strings[sid_a];
    VMString b = chunk->strings[sid_b];
    vm_u32 min_len = (a.length < b.length) ? a.length : b.length;
    for (vm_u32 i = 0; i < min_len; i++) {
        if ((vm_u8)a.chars[i] != (vm_u8)b.chars[i]) {
            return (vm_i32)(vm_u8)a.chars[i] - (vm_i32)(vm_u8)b.chars[i];
        }
    }
    if (a.length < b.length) return -1;
    if (a.length > b.length) return 1;
    return 0;
}

VMReg vm_math_eval(VMReg src, vm_u8 kind) {
    VMReg out = {0};
    switch (kind) {
        case 0: { /* sqrt(f32) */
            vm_f32 x = src.f32;
            if (x <= 0.0f) { out.f32 = 0.0f; break; }
            vm_f32 g = (x > 1.0f) ? (x * 0.5f) : 1.0f;
            for (int i = 0; i < 8; i++) g = 0.5f * (g + x / g);
            out.f32 = g;
            break;
        }
        case 1: { /* floor(f32) */
            vm_i32 iv = (vm_i32)src.f32;
            if ((vm_f32)iv > src.f32) iv--;
            out.f32 = (vm_f32)iv;
            break;
        }
        case 2: { /* ceil(f32) */
            vm_i32 iv = (vm_i32)src.f32;
            if ((vm_f32)iv < src.f32) iv++;
            out.f32 = (vm_f32)iv;
            break;
        }
        case 3: { /* sin(f32) */
            vm_f32 x = src.f32;
            vm_f32 x2 = x * x;
            vm_f32 x3 = x2 * x;
            vm_f32 x5 = x3 * x2;
            vm_f32 x7 = x5 * x2;
            out.f32 = x - (x3 / 6.0f) + (x5 / 120.0f) - (x7 / 5040.0f);
            break;
        }
        case 4: { /* cos(f32) */
            vm_f32 x = src.f32;
            vm_f32 x2 = x * x;
            vm_f32 x4 = x2 * x2;
            vm_f32 x6 = x4 * x2;
            out.f32 = 1.0f - (x2 / 2.0f) + (x4 / 24.0f) - (x6 / 720.0f);
            break;
        }
        case 5: /* abs(f32) */
            out.f32 = (src.f32 < 0.0f) ? -src.f32 : src.f32;
            break;
        case 6: /* abs(i32) */
            out.i32 = (src.i32 < 0) ? -src.i32 : src.i32;
            break;
        case 7: { /* count_ones(i32) */
            vm_u32 u = (vm_u32)src.i32;
            vm_i32 c = 0;
            while (u != 0u) { u &= (u - 1u); c++; }
            out.i32 = c;
            break;
        }
        default:
            break;
    }
    return out;
}

VMReg vm_chunk_cast_value(VMChunk *chunk, VMReg src, vm_u8 type_pair) {
    VMDataType st = (VMDataType)((type_pair >> 4) & 0x0Fu);
    VMDataType dt = (VMDataType)(type_pair & 0x0Fu);
    VMReg out = {0};

    if (st == dt) return src;

    if (st == VM_TYPE_STRING) {
        VMString s = chunk->strings[src.str_id];
        if (dt == VM_TYPE_I32)      out.i32 = (vm_i32)vm_str_parse_i64(s.chars, s.length);
        else if (dt == VM_TYPE_I64) out.i64 = vm_str_parse_i64(s.chars, s.length);
        else if (dt == VM_TYPE_F32) out.f32 = (vm_f32)vm_str_parse_f64(s.chars, s.length);
        else if (dt == VM_TYPE_F64) out.f64 = vm_str_parse_f64(s.chars, s.length);
        return out;
    }

    if (dt == VM_TYPE_STRING) {
        if (st == VM_TYPE_BOOL) {
            out.str_id = (src.b == VM_TRUE) ? vm_chunk_add_string(chunk, "true", 4) : vm_chunk_add_string(chunk, "false", 5);
            return out;
        }
        if (st == VM_TYPE_ARRAY) {
            if (src.arr_id >= chunk->num_arrays) {
                out.str_id = vm_chunk_add_string(chunk, "[]", 2);
                return out;
            }
            VMArray *a = &chunk->arrays[src.arr_id];
            vm_u32 acc = vm_chunk_add_string(chunk, "[", 1);
            vm_u32 comma = vm_chunk_add_string(chunk, ", ", 2);
            vm_u8 epair = (vm_u8)(((vm_u8)a->elem_type << 4) | ((vm_u8)VM_TYPE_STRING & 0x0Fu));
            for (vm_u32 i = 0; i < a->length; i++) {
                if (i > 0) acc = vm_chunk_concat_strings(chunk, acc, comma);
                VMReg es = vm_chunk_cast_value(chunk, a->items[i], epair);
                acc = vm_chunk_concat_strings(chunk, acc, es.str_id);
            }
            vm_u32 rbr = vm_chunk_add_string(chunk, "]", 1);
            out.str_id = vm_chunk_concat_strings(chunk, acc, rbr);
            return out;
        }
        if (st == VM_TYPE_MAP) {
            if (src.map_id >= chunk->num_maps) {
                out.str_id = vm_chunk_add_string(chunk, "{}", 2);
                return out;
            }
            VMMap *m = &chunk->maps[src.map_id];
            vm_u32 acc = vm_chunk_add_string(chunk, "{", 1);
            vm_u32 colon = vm_chunk_add_string(chunk, ": ", 2);
            vm_u32 comma = vm_chunk_add_string(chunk, ", ", 2);
            vm_u8 vpair = (vm_u8)(((vm_u8)m->val_type << 4) | ((vm_u8)VM_TYPE_STRING & 0x0Fu));
            for (vm_u32 i = 0; i < m->length; i++) {
                if (i > 0) acc = vm_chunk_concat_strings(chunk, acc, comma);
                acc = vm_chunk_concat_strings(chunk, acc, m->keys[i]);
                acc = vm_chunk_concat_strings(chunk, acc, colon);
                VMReg vs = vm_chunk_cast_value(chunk, m->vals[i], vpair);
                acc = vm_chunk_concat_strings(chunk, acc, vs.str_id);
            }
            vm_u32 rbr = vm_chunk_add_string(chunk, "}", 1);
            out.str_id = vm_chunk_concat_strings(chunk, acc, rbr);
            return out;
        }
        char *dst = vm_chunk_arena_reserve(chunk, 48);
        if (dst == VM_NULL) return out;
        vm_u32 len;
        if (st == VM_TYPE_F32 || st == VM_TYPE_F64) {
            len = vm_f64_to_str(dst, (st == VM_TYPE_F64) ? src.f64 : (vm_f64)src.f32, vm_bool_from_int(st == VM_TYPE_F32));
        } else {
            len = vm_i64_to_str(dst, (st == VM_TYPE_I64) ? src.i64 : (vm_i64)src.i32);
        }
        dst[len] = '\0';
        out.str_id = commit_arena_string(chunk, dst, len);
        return out;
    }

    if ((st == VM_TYPE_I32 || st == VM_TYPE_I64) && (dt == VM_TYPE_I32 || dt == VM_TYPE_I64)) {
        vm_i64 v = (st == VM_TYPE_I64) ? src.i64 : (vm_i64)src.i32;
        if (dt == VM_TYPE_I64) out.i64 = v;
        else out.i32 = (vm_i32)v;
        return out;
    }
    vm_f64 num = (st == VM_TYPE_F64) ? src.f64 :
                 (st == VM_TYPE_F32) ? (vm_f64)src.f32 :
                 (st == VM_TYPE_I64) ? (vm_f64)src.i64 : (vm_f64)src.i32;
    if (dt == VM_TYPE_I32)      out.i32 = (vm_i32)num;
    else if (dt == VM_TYPE_I64) out.i64 = (vm_i64)num;
    else if (dt == VM_TYPE_F32) out.f32 = (vm_f32)num;
    else if (dt == VM_TYPE_F64) out.f64 = num;
    else if (dt == VM_TYPE_BOOL) out.b  = vm_bool_from_int(num != 0.0);
    return out;
}

#define VM_MAX_ARRAY_LEN (1u << 26)

static vm_bool array_reserve(VMChunk *chunk, VMArray *a, vm_u32 min_cap) {
    if (min_cap <= a->capacity) return VM_TRUE;
    if (min_cap > VM_MAX_ARRAY_LEN) return VM_FALSE;
    vm_u32 new_cap = (a->capacity > 0) ? a->capacity * 2 : VM_INIT_ARRAY_CAP;
    while (new_cap < min_cap) new_cap *= 2;
    VMReg *items = (VMReg *)vm_chunk_realloc(chunk, a->items, sizeof(VMReg) * a->capacity, sizeof(VMReg) * new_cap);
    if (items == VM_NULL) return VM_FALSE;
    vm_chunk_gc_account(chunk, (vm_u64)sizeof(VMReg) * (new_cap - a->capacity));
    a->items = items;
    a->capacity = new_cap;
    return VM_TRUE;
}

vm_u32 vm_chunk_array_new(VMChunk *chunk, VMDataType elem_type) {
    vm_chunk_gc_account(chunk, sizeof(VMArray));
    if (chunk->free_arrays.count > 0) {
        vm_u32 id = chunk->free_arrays.ids[--chunk->free_arrays.count];
        chunk->arrays[id].items = VM_NULL;
        chunk->arrays[id].capacity = 0;
        chunk->arrays[id].elem_type = elem_type;
        chunk->arrays[id].length = 0;
        chunk->arrays[id].dead = VM_FALSE;
        return id;
    }
    if (chunk->num_arrays >= chunk->arrays_cap) {
        vm_u32 new_cap = (chunk->arrays_cap > 0) ? chunk->arrays_cap * 2 : VM_INIT_ARRAY_POOL;
        VMArray *pool = (VMArray *)vm_chunk_realloc(chunk, chunk->arrays,
                                                    sizeof(VMArray) * chunk->arrays_cap, sizeof(VMArray) * new_cap);
        if (pool == VM_NULL) return 0;
        chunk->arrays = pool;
        chunk->arrays_cap = new_cap;
    }
    vm_u32 id = chunk->num_arrays++;
    chunk->arrays[id].items = VM_NULL;
    chunk->arrays[id].capacity = 0;
    chunk->arrays[id].elem_type = elem_type;
    chunk->arrays[id].length = 0;
    chunk->arrays[id].dead = VM_FALSE;
    return id;
}

VMReg vm_chunk_array_get(const VMChunk *chunk, vm_u32 arr_id, vm_i32 idx) {
    VMReg zero = {0};
    if (arr_id >= chunk->num_arrays) return zero;
    const VMArray *a = &chunk->arrays[arr_id];
    if (idx < 0 || (vm_u32)idx >= a->length) return zero;
    return a->items[idx];
}

void vm_chunk_array_set(VMChunk *chunk, vm_u32 arr_id, vm_i32 idx, VMReg val) {
    if (arr_id >= chunk->num_arrays || idx < 0) return;
    VMArray *a = &chunk->arrays[arr_id];
    if ((vm_u32)idx >= a->length) {
        if (array_reserve(chunk, a, (vm_u32)idx + 1) == VM_FALSE) return;
        for (vm_u32 k = a->length; k <= (vm_u32)idx; k++) a->items[k] = (VMReg){0};
        a->length = (vm_u32)idx + 1;
    }
    a->items[idx] = val;
}

void vm_chunk_array_push(VMChunk *chunk, vm_u32 arr_id, VMReg val) {
    if (arr_id >= chunk->num_arrays) return;
    VMArray *a = &chunk->arrays[arr_id];
    if (a->length >= a->capacity && array_reserve(chunk, a, a->length + 1) == VM_FALSE) return;
    a->items[a->length++] = val;
}

VMReg vm_chunk_array_pop(VMChunk *chunk, vm_u32 arr_id) {
    VMReg zero = {0};
    if (arr_id >= chunk->num_arrays) return zero;
    VMArray *a = &chunk->arrays[arr_id];
    if (a->length == 0) return zero;
    return a->items[--a->length];
}

vm_u32 vm_chunk_map_new(VMChunk *chunk, VMDataType key_type, VMDataType val_type) {
    vm_chunk_gc_account(chunk, sizeof(VMMap));
    if (chunk->free_maps.count > 0) {
        vm_u32 id = chunk->free_maps.ids[--chunk->free_maps.count];
        chunk->maps[id].keys = VM_NULL;
        chunk->maps[id].vals = VM_NULL;
        chunk->maps[id].capacity = 0;
        chunk->maps[id].key_type = key_type;
        chunk->maps[id].val_type = val_type;
        chunk->maps[id].length = 0;
        chunk->maps[id].dead = VM_FALSE;
        return id;
    }
    if (chunk->num_maps >= chunk->maps_cap) {
        vm_u32 new_cap = (chunk->maps_cap > 0) ? chunk->maps_cap * 2 : VM_INIT_MAP_POOL;
        VMMap *pool = (VMMap *)vm_chunk_realloc(chunk, chunk->maps,
                                                sizeof(VMMap) * chunk->maps_cap, sizeof(VMMap) * new_cap);
        if (pool == VM_NULL) return 0;
        chunk->maps = pool;
        chunk->maps_cap = new_cap;
    }
    vm_u32 id = chunk->num_maps++;
    chunk->maps[id].keys = VM_NULL;
    chunk->maps[id].vals = VM_NULL;
    chunk->maps[id].capacity = 0;
    chunk->maps[id].key_type = key_type;
    chunk->maps[id].val_type = val_type;
    chunk->maps[id].length = 0;
    chunk->maps[id].dead = VM_FALSE;
    return id;
}

/* Keys are interned, so the cached hash rejects almost every non-match before comparing bytes */
static vm_i32 map_find(const VMChunk *chunk, const VMMap *m, vm_u32 key_sid) {
    vm_u32 h = chunk->string_hashes[key_sid];
    for (vm_u32 i = 0; i < m->length; i++) {
        vm_u32 k = m->keys[i];
        if (k == key_sid) return (vm_i32)i;
        if (k < chunk->num_strings && chunk->string_hashes[k] == h &&
            vm_chunk_str_cmp(chunk, k, key_sid) == 0) {
            return (vm_i32)i;
        }
    }
    return -1;
}

VMReg vm_chunk_map_get(const VMChunk *chunk, vm_u32 map_id, vm_u32 key_sid) {
    VMReg zero = {0};
    if (map_id >= chunk->num_maps) return zero;
    const VMMap *m = &chunk->maps[map_id];
    vm_i32 i = map_find(chunk, m, key_sid);
    return (i >= 0) ? m->vals[i] : zero;
}

void vm_chunk_map_set(VMChunk *chunk, vm_u32 map_id, vm_u32 key_sid, VMReg val) {
    if (map_id >= chunk->num_maps) return;
    VMMap *m = &chunk->maps[map_id];
    vm_i32 i = map_find(chunk, m, key_sid);
    if (i >= 0) {
        m->vals[i] = val;
        return;
    }
    if (m->length >= m->capacity) {
        vm_u32 new_cap = (m->capacity > 0) ? m->capacity * 2 : VM_INIT_MAP_CAP;
        vm_u32 *keys = (vm_u32 *)vm_chunk_realloc(chunk, m->keys, sizeof(vm_u32) * m->capacity, sizeof(vm_u32) * new_cap);
        if (keys == VM_NULL) return;
        m->keys = keys;
        VMReg *vals = (VMReg *)vm_chunk_realloc(chunk, m->vals, sizeof(VMReg) * m->capacity, sizeof(VMReg) * new_cap);
        if (vals == VM_NULL) return;
        m->vals = vals;
        vm_chunk_gc_account(chunk, (vm_u64)(sizeof(VMReg) + sizeof(vm_u32)) * (new_cap - m->capacity));
        m->capacity = new_cap;
    }
    m->keys[m->length] = key_sid;
    m->vals[m->length] = val;
    m->length++;
}

vm_bool vm_chunk_map_has(const VMChunk *chunk, vm_u32 map_id, vm_u32 key_sid) {
    if (map_id >= chunk->num_maps) return VM_FALSE;
    return vm_bool_from_int(map_find(chunk, &chunk->maps[map_id], key_sid) >= 0);
}

vm_bool vm_chunk_map_del(VMChunk *chunk, vm_u32 map_id, vm_u32 key_sid) {
    if (map_id >= chunk->num_maps) return VM_FALSE;
    VMMap *m = &chunk->maps[map_id];
    vm_i32 i = map_find(chunk, m, key_sid);
    if (i < 0) return VM_FALSE;
    for (vm_u32 j = (vm_u32)i; j + 1 < m->length; j++) {
        m->keys[j] = m->keys[j + 1];
        m->vals[j] = m->vals[j + 1];
    }
    m->length--;
    return VM_TRUE;
}

vm_u32 vm_chunk_map_key_at(const VMChunk *chunk, vm_u32 map_id, vm_i32 idx) {
    if (map_id >= chunk->num_maps) return 0;
    const VMMap *m = &chunk->maps[map_id];
    if (idx < 0 || (vm_u32)idx >= m->length) return 0;
    return m->keys[idx];
}

VMReg vm_chunk_map_val_at(const VMChunk *chunk, vm_u32 map_id, vm_i32 idx) {
    VMReg zero = {0};
    if (map_id >= chunk->num_maps) return zero;
    const VMMap *m = &chunk->maps[map_id];
    if (idx < 0 || (vm_u32)idx >= m->length) return zero;
    return m->vals[idx];
}

/* ---------------------------------------------------------------------------
 * Closures & cells
 * ------------------------------------------------------------------------- */

static vm_bool grow_pool(VMChunk *chunk, void **ptr, vm_u32 *cap, vm_u32 need, vm_u32 elem, vm_u32 init) {
    if (need <= *cap) return VM_TRUE;
    vm_u32 nc = (*cap > 0) ? *cap * 2 : init;
    while (nc < need) nc *= 2;
    void *np = vm_chunk_realloc(chunk, *ptr, (vm_size)elem * *cap, (vm_size)elem * nc);
    if (np == VM_NULL) return VM_FALSE;
    *ptr = np;
    *cap = nc;
    return VM_TRUE;
}

vm_u32 vm_chunk_cell_new(VMChunk *chunk, VMReg val, VMDataType type) {
    vm_chunk_gc_account(chunk, sizeof(VMReg) + 4);
    if (chunk->free_cells.count > 0) {
        vm_u32 id = chunk->free_cells.ids[--chunk->free_cells.count];
        chunk->cells[id] = val;
        chunk->cell_types[id] = (vm_u8)type;
        return id;
    }
    if (chunk->num_cells >= chunk->cells_cap) {
        vm_u32 old_cap = chunk->cells_cap;
        if (grow_pool(chunk, (void **)&chunk->cells, &chunk->cells_cap, chunk->num_cells + 1, sizeof(VMReg), 64) == VM_FALSE) return 0;
        vm_u8 *types = (vm_u8 *)vm_chunk_realloc(chunk, chunk->cell_types, old_cap, chunk->cells_cap);
        if (types == VM_NULL) return 0;
        chunk->cell_types = types;
    }
    chunk->cells[chunk->num_cells] = val;
    chunk->cell_types[chunk->num_cells] = (vm_u8)type;
    return chunk->num_cells++;
}

vm_i32 vm_chunk_closure_new(VMChunk *chunk, vm_u32 func_id, vm_u8 num_cells) {
    vm_chunk_gc_account(chunk, sizeof(VMClosure) + (vm_u64)num_cells * 4);
    if (grow_pool(chunk, (void **)&chunk->closure_cells, &chunk->closure_cells_cap,
                  chunk->num_closure_cells + num_cells, sizeof(vm_u32), 64) == VM_FALSE) return -1;
    vm_u32 id;
    if (chunk->free_closures.count > 0) {
        id = chunk->free_closures.ids[--chunk->free_closures.count];
    } else {
        if (grow_pool(chunk, (void **)&chunk->closures, &chunk->closures_cap, chunk->num_closures + 1, sizeof(VMClosure), 32) == VM_FALSE) return -1;
        id = chunk->num_closures++;
    }
    VMClosure *cl = &chunk->closures[id];
    cl->func_id = func_id;
    cl->first_cell = chunk->num_closure_cells;
    cl->num_cells = num_cells;
    cl->dead = VM_FALSE;
    chunk->num_closure_cells += num_cells;
    return (vm_i32)id;
}

vm_bool vm_chunk_decode_func(const VMChunk *chunk, vm_i32 value, vm_u32 *out_func_id, vm_i32 *out_closure) {
    if (value >= 0 && VM_IS_CLOSURE(value)) {
        vm_u32 cid = (vm_u32)(value & ~VM_CLOSURE_TAG);
        if (cid >= chunk->num_closures) return VM_FALSE;
        *out_func_id = chunk->closures[cid].func_id;
        *out_closure = (vm_i32)cid;
        return VM_TRUE;
    }
    if (value < 0 || (vm_u32)value >= chunk->num_functions) return VM_FALSE;
    *out_func_id = (vm_u32)value;
    *out_closure = -1;
    return VM_TRUE;
}
