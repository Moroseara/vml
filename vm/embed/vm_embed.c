#include "vm_embed.h"
#include "../string/vm_string.h"
#include "../stdlib/vm_stdlib.h"

void vm_config_init_default(VMConfig *cfg) {
    cfg->features = VM_FEAT_ALL;
    cfg->max_instructions = 0;
    cfg->lib_path[0] = '\0';
    cfg->num_pkg_mounts = 0;
    cfg->num_denied = 0;
}

void vm_config_init_embedded(VMConfig *cfg, vm_u32 features, vm_u64 max_instructions) {
    cfg->features = features;
    cfg->max_instructions = max_instructions;
    cfg->lib_path[0] = '\0';
    cfg->num_pkg_mounts = 0;
    cfg->num_denied = 0;
}

void vm_config_set_lib_path(VMConfig *cfg, const char *path) {
    vm_str_copy(cfg->lib_path, path, vm_str_len(path), 128);
}

void vm_config_mount_package(VMConfig *cfg, const char *pkg_name, const char *dir_path) {
    if (cfg->num_pkg_mounts >= VM_MAX_PKG_MOUNTS) return;
    VMPackageMount *m = &cfg->pkg_mounts[cfg->num_pkg_mounts++];
    vm_str_copy(m->pkg_name, pkg_name, vm_str_len(pkg_name), 32);
    vm_str_copy(m->dir_path, dir_path, vm_str_len(dir_path), 128);
}

const char *vm_config_resolve_package(const VMConfig *cfg, const char *pkg_name, vm_u32 pkglen) {
    if (cfg == VM_NULL) return VM_NULL;
    for (vm_u32 i = 0; i < cfg->num_pkg_mounts; i++) {
        vm_u32 mlen = vm_str_len(cfg->pkg_mounts[i].pkg_name);
        if (vm_str_eq(cfg->pkg_mounts[i].pkg_name, mlen, pkg_name, pkglen) == VM_TRUE) {
            return cfg->pkg_mounts[i].dir_path;
        }
    }
    return VM_NULL;
}

void vm_config_enable(VMConfig *cfg, vm_u32 feature_flag) {
    cfg->features |= feature_flag;
}

void vm_config_disable(VMConfig *cfg, vm_u32 feature_flag) {
    cfg->features &= ~feature_flag;
}

vm_bool vm_config_has(const VMConfig *cfg, vm_u32 feature_flag) {
    if (cfg == VM_NULL) return VM_TRUE;
    return vm_bool_from_int((cfg->features & feature_flag) != 0);
}

vm_bool vm_embed_load_stdlib(VMChunk *chunk, const char *pkg, vm_u32 pkglen, const char *mod, vm_u32 modlen) {
    if (vm_str_eq_cstr(pkg, pkglen, "std") == VM_TRUE || pkglen == modlen) {
        if (vm_str_eq_cstr(mod, modlen, "math") == VM_TRUE ||
            vm_str_eq_cstr(mod, modlen, "random") == VM_TRUE) {
            vm_stdlib_register_math(chunk);
            return VM_TRUE;
        }
        if (vm_str_eq_cstr(mod, modlen, "io") == VM_TRUE ||
            vm_str_eq_cstr(mod, modlen, "coroutine") == VM_TRUE) {
            vm_stdlib_register_io(chunk);
            return VM_TRUE;
        }
        if (vm_str_eq_cstr(mod, modlen, "string") == VM_TRUE) {
            vm_stdlib_register_string(chunk);
            return VM_TRUE;
        }
        if (vm_str_eq_cstr(mod, modlen, "array") == VM_TRUE) {
            vm_stdlib_register_array(chunk);
            return VM_TRUE;
        }
        if (vm_str_eq_cstr(mod, modlen, "map") == VM_TRUE) {
            vm_stdlib_register_map(chunk);
            return VM_TRUE;
        }
        if (vm_str_eq_cstr(mod, modlen, "bit") == VM_TRUE) {
            vm_stdlib_register_bit(chunk);
            return VM_TRUE;
        }
        if (vm_str_eq_cstr(mod, modlen, "task") == VM_TRUE) {
            vm_stdlib_register_task(chunk);
            return VM_TRUE;
        }
        if (vm_str_eq_cstr(mod, modlen, "debug") == VM_TRUE ||
            vm_str_eq_cstr(mod, modlen, "debug_lib") == VM_TRUE) {
            vm_stdlib_register_debug(chunk);
            return VM_TRUE;
        }
        if (vm_str_eq_cstr(mod, modlen, "time") == VM_TRUE) {
            vm_stdlib_register_time(chunk);
            return VM_TRUE;
        }
        if (vm_str_eq_cstr(mod, modlen, "env") == VM_TRUE) {
            vm_stdlib_register_env(chunk);
            return VM_TRUE;
        }
        if (vm_str_eq_cstr(mod, modlen, "os") == VM_TRUE) {
            vm_stdlib_register_os(chunk);
            return VM_TRUE;
        }
    }
    return VM_FALSE;
}

vm_i32 vm_embed_find_func(const VMChunk *chunk, const char *func_name) {
    vm_u32 nlen = vm_str_len(func_name);
    for (vm_u32 i = 0; i < chunk->num_functions; i++) {
        vm_u32 flen = vm_str_len(chunk->functions[i].name);
        if (vm_str_eq(chunk->functions[i].name, flen, func_name, nlen) == VM_TRUE) {
            return (vm_i32)i;
        }
    }
    return -1;
}

VMReg vm_embed_call_func(VMCPU *cpu, VMChunk *chunk, vm_i32 func_id,
                         const VMReg *args, vm_u8 argc) {
    VMReg zero = {0};
    if (func_id < 0 || (vm_u32)func_id >= chunk->num_functions) {
        return zero;
    }
    vm_cpu_init(cpu);
    for (vm_u8 i = 0; i < argc; i++) {
        cpu->registers[i] = args[i];
    }
    vm_i32 saved_entry = chunk->entry_func_id;
    chunk->entry_func_id = func_id;
    VMReg res = vm_cpu_execute(cpu, chunk);
    chunk->entry_func_id = saved_entry;
    return res;
}

vm_bool vm_config_deny_module(VMConfig *cfg, const char *name) {
    if (cfg->num_denied >= VM_MAX_DENIED) return VM_FALSE;
    vm_str_copy(cfg->denied[cfg->num_denied++], name, vm_str_len(name), 64);
    return VM_TRUE;
}

vm_bool vm_config_module_denied(const VMConfig *cfg, const char *full, vm_u32 len) {
    if (cfg == VM_NULL) return VM_FALSE;
    static const struct { const char *name; vm_u32 flag; } FEATURES[] = {
        {"std.io", VM_FEAT_STD_IO}, {"std.coroutine", VM_FEAT_COROUTINE}, {"std.random", VM_FEAT_STD_RANDOM},
        {"std.env", VM_FEAT_STD_ENV}, {"std.fs", VM_FEAT_STD_FS}, {"std.math", VM_FEAT_STD_MATH},
    };
    for (vm_u32 i = 0; i < sizeof(FEATURES) / sizeof(FEATURES[0]); i++) {
        if (vm_str_eq_cstr(full, len, FEATURES[i].name) == VM_TRUE && (cfg->features & FEATURES[i].flag) == 0) return VM_TRUE;
    }
    for (vm_u32 i = 0; i < cfg->num_denied; i++) {
        const char *d = cfg->denied[i];
        vm_u32 dl = vm_str_len(d);
        if (vm_str_eq(full, len, d, dl) == VM_TRUE) return VM_TRUE;
        /* "pkg.*" covers pkg itself and everything under it */
        if (dl >= 2 && d[dl - 1] == '*' && d[dl - 2] == '.') {
            vm_u32 pl = dl - 2;
            if (len >= pl && vm_str_eq(full, pl, d, pl) == VM_TRUE && (len == pl || full[pl] == '.')) return VM_TRUE;
        }
    }
    return VM_FALSE;
}
