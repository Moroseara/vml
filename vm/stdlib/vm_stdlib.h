#ifndef VM_STDLIB_H
#define VM_STDLIB_H

#include "../core/vm_types.h"
#include "../memory/vm_memory.h"

/*
 * ============================================================================
 * LUAU-STYLE NATIVE VM STANDARD LIBRARY & PRIMITIVE MODULES (`vm/stdlib/`)
 * Built directly into `VMChunk` via C API (emitting pure VM bytecode opcodes,
 * `VMNativeFn` callbacks, and `vm_lib_bind_primitive` metatable bindings):
 *   - `vm/stdlib/std_math.c`   (`std.math`, `std.random`)
 *   - `vm/stdlib/std_string.c` (`std.string`, primitive `string` -> `String`)
 *   - `vm/stdlib/std_array.c`  (`std.array`,  primitive `array`  -> `Array`)
 *   - `vm/stdlib/std_map.c`    (`std.map`,    primitive `map`    -> `Map`)
 *   - `vm/stdlib/std_bit.c`    (`std.bit`)
 *   - `vm/stdlib/std_io.c`     (`std.io`, `std.coroutine`)
 *   - `vm/stdlib/std_task.c`   (`std.task`, `std.debug`, `std.time`)
 * ============================================================================
 */

void vm_stdlib_register_prelude(VMChunk *chunk);
void vm_stdlib_register_prelude_core(VMChunk *chunk);   /* AST compiler: String/Array methods come from the std .vml prelude */
void vm_stdlib_register_string_core(VMChunk *chunk);
void vm_stdlib_register_array_core(VMChunk *chunk);
void vm_stdlib_register_map_core(VMChunk *chunk);
void vm_stdlib_register_math(VMChunk *chunk);
void vm_stdlib_register_string(VMChunk *chunk);
void vm_stdlib_register_array(VMChunk *chunk);
void vm_stdlib_register_map(VMChunk *chunk);
void vm_stdlib_register_bit(VMChunk *chunk);
void vm_stdlib_register_io(VMChunk *chunk);
void vm_stdlib_register_task(VMChunk *chunk);
void vm_stdlib_register_debug(VMChunk *chunk);
void vm_stdlib_register_time(VMChunk *chunk);
void vm_stdlib_register_env(VMChunk *chunk);
void vm_stdlib_register_os(VMChunk *chunk);


/* Natives reused as intrinsics by the .vml stdlib (vm_intrinsics.c) */
VMReg native_task_spawn(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_task_defer(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_task_delay(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_task_cancel(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_task_step(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_task_status(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_task_result(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_task_error(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_task_ok(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_gc(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_gc_count(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_live_arrays(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_live_strings(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_func_count(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_bytecode_size(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_string_count(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_arena_used(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_array_count(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_map_count(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_func_name(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_traceback(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_dbg_last_error(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_os_platform(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_os_arch(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_os_clock(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_os_time(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_os_getenv(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_map_clear(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VMReg native_map_keys(VMChunk *chunk, const VMReg *args, vm_u8 argc);

#endif /* VM_STDLIB_H */
