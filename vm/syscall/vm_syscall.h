#ifndef VM_SYSCALL_H
#define VM_SYSCALL_H

#include "../core/vm_types.h"
#include "../string/vm_string.h"

/*
 * ============================================================================
 * VM KERNEL SYSCALL & WEB PLAYGROUND I/O SUBSYSTEM (`vm/syscall/vm_syscall.h`)
 * Supports both direct OS Kernel `write` syscall AND in-memory Output Capture
 * Buffer / Callback Hooks for WebAssembly (Wasm) Browser Playgrounds & Embeds!
 * ============================================================================
 */

typedef void (*VMOutputCallback)(const char *buf, vm_size len);

void        vm_sys_set_output_callback(VMOutputCallback cb);
void        vm_sys_enable_output_buffer(vm_bool enable);
const char *vm_sys_get_output_buffer(vm_u32 *out_len);
void        vm_sys_clear_output_buffer(void);
void        vm_sys_set_output_silent(vm_bool silent);   /* with the buffer enabled: capture without printing */

void        vm_sys_write(const char *buf, vm_size len);
void        vm_sys_write_cstr(const char *str);
void        vm_sys_print_i32(vm_i32 val);
void        vm_sys_print_i64(vm_i64 val);
void        vm_sys_print_f32(vm_f32 val);
void        vm_sys_print_f64(vm_f64 val);
void        vm_sys_print_bool(vm_bool val);
void        vm_sys_print_str(const char *chars, vm_u32 len);
vm_u64      vm_sys_clock_ns(void);
vm_i64      vm_sys_unix_time(void);
const char *vm_sys_getenv(const char *key);

/* Files (used by the `fs` intrinsics). Paths are NUL-terminated. */
char       *vm_sys_file_read(const char *path, vm_u32 *out_len);   /* malloc'd buffer (free with vm_sys_file_free) or NULL */
void        vm_sys_file_free(char *buf, vm_u32 len);
vm_bool     vm_sys_file_write(const char *path, const char *data, vm_u32 len, vm_bool append);
vm_bool     vm_sys_file_exists(const char *path);
vm_bool     vm_sys_file_remove(const char *path);
const char *vm_sys_platform(void);
const char *vm_sys_arch(void);

/* Default heap hook used when the host does not supply its own `VMReallocFn`. */
void       *vm_sys_default_realloc(void *user_data, void *ptr, vm_size old_size, vm_size new_size);

#endif /* VM_SYSCALL_H */
