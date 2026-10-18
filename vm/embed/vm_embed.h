#ifndef VM_EMBED_H
#define VM_EMBED_H

#include "../core/vm_types.h"
#include "../memory/vm_memory.h"
#include "../cpu/vm_cpu.h"

/*
 * ============================================================================
 * VM KERNEL EMBEDDING, CAPABILITY & BUILT-IN VFS (`vm/embed/vm_embed.h`)
 * Allows Host Applications (Game Engines, Servers, CLI) to selectively
 * enable/disable language features (`std.io`, `coroutine`, `std.random`, etc.),
 * mount custom package roots (`import myStd`), configure `--lib-path`, and
 * fall back to Built-in VFS (`std.*`) when `std/` is not present on disk.
 * ============================================================================
 */

typedef enum {
    VM_FEAT_NONE          = 0,
    VM_FEAT_STD_MATH      = (1u << 0), /* Allow `std.math` */
    VM_FEAT_STD_RANDOM    = (1u << 1), /* Allow `std.random` */
    VM_FEAT_STD_IO        = (1u << 2), /* Allow `std.io` (File/Console I/O) */
    VM_FEAT_COROUTINE     = (1u << 3), /* Allow `std.coroutine` & `yield` */
    VM_FEAT_ARRAY         = (1u << 4), /* Allow Array literals `[10, 20, 30]` */
    VM_FEAT_SANDBOX_LIMIT = (1u << 5), /* Enforce `max_instructions` fuel limit */
    VM_FEAT_STD_FS        = (1u << 6), /* Allow `std.fs` (read / write files) */
    VM_FEAT_STD_ENV       = (1u << 7), /* Allow `std.env` (read OS environment variables) */

    /* Preset Profiles */
    VM_FEAT_SAFE_EMBED    = (VM_FEAT_STD_MATH | VM_FEAT_STD_RANDOM | VM_FEAT_ARRAY | VM_FEAT_SANDBOX_LIMIT),
    VM_FEAT_ALL           = 0xFFFFFFFFu
} VMFeatureFlags;

#define VM_MAX_PKG_MOUNTS 8

typedef struct {
    char pkg_name[32];
    char dir_path[128];
} VMPackageMount;

typedef struct {
    vm_u32         features;         /* Bitmask of `VMFeatureFlags` */
    vm_u64         max_instructions; /* 0 = unlimited, >0 = max instructions per call */
    char           lib_path[128];    /* Custom library search path (`--lib-path`) */
    VMPackageMount pkg_mounts[VM_MAX_PKG_MOUNTS];
    vm_u32         num_pkg_mounts;
} VMConfig;

void        vm_config_init_default(VMConfig *cfg);
void        vm_config_init_embedded(VMConfig *cfg, vm_u32 features, vm_u64 max_instructions);
void        vm_config_set_lib_path(VMConfig *cfg, const char *path);
void        vm_config_mount_package(VMConfig *cfg, const char *pkg_name, const char *dir_path);
const char *vm_config_resolve_package(const VMConfig *cfg, const char *pkg_name, vm_u32 pkglen);
void        vm_config_enable(VMConfig *cfg, vm_u32 feature_flag);
void        vm_config_disable(VMConfig *cfg, vm_u32 feature_flag);
vm_bool     vm_config_has(const VMConfig *cfg, vm_u32 feature_flag);

/* Native C Standard Library Loader (`std.*`) directly into `VMChunk` */
vm_bool     vm_embed_load_stdlib(VMChunk *chunk, const char *pkg, vm_u32 pkglen, const char *mod, vm_u32 modlen);

/* Host -> Script Function Call API (e.g., calling `on_update(dt)` from C Host) */
vm_i32      vm_embed_find_func(const VMChunk *chunk, const char *func_name);
VMReg       vm_embed_call_func(VMCPU *cpu, VMChunk *chunk, vm_i32 func_id,
                               const VMReg *args, vm_u8 argc);

#endif /* VM_EMBED_H */
