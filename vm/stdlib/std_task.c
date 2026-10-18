#include "vm_stdlib.h"
#include "../cpu/vm_cpu.h"
#include "../syscall/vm_syscall.h"
#include "../syscall/vm_syscall.h"
#include "../embed/vm_embed.h"

/*
 * `std.task`, `std.debug`, `std.time` & Prelude Registration (`vm/stdlib/std_task.c`)
 * Implements:
 *   1. `Task` (`std.task`) Fiber / Task Scheduler (`spawn`, `defer`, `delay`, `cancel`, `step`, `status`, `result`)
 *   2. `Debug` (`std.debug`) VM Introspection (`func_count`, `bytecode_size`, `string_count`, `arena_used`, `array_count`, `map_count`, `func_name`)
 *   3. `Time` (`std.time`) High-Resolution Clock (`now_ns`, `now_ms`, `now_sec`)
 */

static void run_task_entry(VMChunk *chunk, VMTaskEntry *t) {
    vm_u32 fid = 0;
    vm_i32 closure = -1;
    if (vm_chunk_decode_func(chunk, t->func_id, &fid, &closure) == VM_FALSE) {
        t->status = 4;
        t->error_str_id = vm_chunk_add_string(chunk, "invalid task function", 21);
        return;
    }
    VMCPU fiber_cpu;
    vm_cpu_init(&fiber_cpu);
    fiber_cpu.cur_closure = closure;
    fiber_cpu.catch_top_level = VM_TRUE;
    fiber_cpu.registers[0] = t->arg;
    vm_i32 saved_entry = chunk->entry_func_id;
    chunk->entry_func_id = (vm_i32)fid;
    t->result = vm_cpu_execute(&fiber_cpu, chunk);
    chunk->entry_func_id = saved_entry;
    if (fiber_cpu.has_error == VM_TRUE) {
        t->status = 4; /* errored */
        t->error_str_id = fiber_cpu.last_error_str_id;
    } else {
        t->status = 2; /* completed */
        t->error_str_id = 0;
    }
}

VMReg native_task_spawn(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (chunk->num_tasks >= VM_MAX_TASKS) {
        out.i32 = -1;
        return out;
    }
    vm_u32 tid = chunk->num_tasks++;
    VMTaskEntry *t = &chunk->tasks[tid];
    t->func_id = args[0].i32;
    t->arg = args[1];
    t->delay_ms = 0;
    t->error_str_id = 0;
    t->status = 1; /* running */

    run_task_entry(chunk, t);
    out.i32 = (vm_i32)tid;
    return out;
}

VMReg native_task_defer(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (chunk->num_tasks >= VM_MAX_TASKS) {
        out.i32 = -1;
        return out;
    }
    vm_u32 tid = chunk->num_tasks++;
    VMTaskEntry *t = &chunk->tasks[tid];
    t->func_id = args[0].i32;
    t->arg = args[1];
    t->result = (VMReg){0};
    t->delay_ms = 0;
    t->error_str_id = 0;
    t->status = 0; /* pending / deferred */
    out.i32 = (vm_i32)tid;
    return out;
}

VMReg native_task_delay(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (chunk->num_tasks >= VM_MAX_TASKS) {
        out.i32 = -1;
        return out;
    }
    vm_u32 tid = chunk->num_tasks++;
    VMTaskEntry *t = &chunk->tasks[tid];
    t->delay_ms = args[0].i32;
    t->func_id = args[1].i32;
    t->arg = args[2];
    t->result = (VMReg){0};
    t->error_str_id = 0;
    t->status = 0; /* pending / delayed */
    out.i32 = (vm_i32)tid;
    return out;
}

VMReg native_task_cancel(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    vm_i32 tid = args[0].i32;
    if (tid < 0 || (vm_u32)tid >= chunk->num_tasks) {
        out.i32 = (vm_i32)VM_FALSE;
        return out;
    }
    if (chunk->tasks[tid].status == 0) {
        chunk->tasks[tid].status = 3; /* cancelled */
        out.i32 = (vm_i32)VM_TRUE;
        return out;
    }
    out.i32 = (vm_i32)VM_FALSE;
    return out;
}

VMReg native_task_step(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args;
    (void)argc;
    VMReg out = {0};
    vm_i32 ran = 0;
    for (vm_u32 i = 0; i < chunk->num_tasks; i++) {
        VMTaskEntry *t = &chunk->tasks[i];
        if (t->status == 0) {
            t->status = 1;
            run_task_entry(chunk, t);
            ran++;
        }
    }
    out.i32 = ran;
    return out;
}

VMReg native_task_status(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    vm_i32 tid = args[0].i32;
    if (tid < 0 || (vm_u32)tid >= chunk->num_tasks) {
        out.str_id = vm_chunk_add_string(chunk, "invalid", 7);
        return out;
    }
    vm_u8 st = chunk->tasks[tid].status;
    if (st == 0)      out.str_id = vm_chunk_add_string(chunk, "deferred", 8);
    else if (st == 1) out.str_id = vm_chunk_add_string(chunk, "running", 7);
    else if (st == 2) out.str_id = vm_chunk_add_string(chunk, "completed", 9);
    else if (st == 4) out.str_id = vm_chunk_add_string(chunk, "errored", 7);
    else              out.str_id = vm_chunk_add_string(chunk, "cancelled", 9);
    return out;
}

VMReg native_task_result(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    vm_i32 tid = args[0].i32;
    if (tid >= 0 && (vm_u32)tid < chunk->num_tasks) {
        return chunk->tasks[tid].result;
    }
    return out;
}

VMReg native_task_error(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    vm_i32 tid = args[0].i32;
    if (tid >= 0 && (vm_u32)tid < chunk->num_tasks) {
        out.str_id = chunk->tasks[tid].error_str_id;
    }
    return out;
}

VMReg native_task_ok(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    vm_i32 tid = args[0].i32;
    if (tid >= 0 && (vm_u32)tid < chunk->num_tasks) {
        out.i32 = (vm_i32)vm_bool_from_int(chunk->tasks[tid].status == 2);
    }
    return out;
}

void vm_stdlib_register_task(VMChunk *chunk) {
    (void)vm_lib_add_struct(chunk, "Task", "task");

    {
        VMDataType pt[2] = {VM_TYPE_I32, VM_TYPE_I32};
        vm_u32 fid = vm_lib_add_native(chunk, "Task.spawn", 2, pt, VM_TYPE_I32, native_task_spawn);
        chunk->functions[fid].min_arity = 1;
        chunk->functions[fid].param_has_default[1] = VM_TRUE;
        chunk->functions[fid].param_defaults[1].i32 = 0;
    }
    {
        VMDataType pt[2] = {VM_TYPE_I32, VM_TYPE_I32};
        vm_u32 fid = vm_lib_add_native(chunk, "Task.defer", 2, pt, VM_TYPE_I32, native_task_defer);
        chunk->functions[fid].min_arity = 1;
        chunk->functions[fid].param_has_default[1] = VM_TRUE;
        chunk->functions[fid].param_defaults[1].i32 = 0;
    }
    {
        VMDataType pt[3] = {VM_TYPE_I32, VM_TYPE_I32, VM_TYPE_I32};
        vm_u32 fid = vm_lib_add_native(chunk, "Task.delay", 3, pt, VM_TYPE_I32, native_task_delay);
        chunk->functions[fid].min_arity = 2;
        chunk->functions[fid].param_has_default[2] = VM_TRUE;
        chunk->functions[fid].param_defaults[2].i32 = 0;
    }
    {
        VMDataType pt[1] = {VM_TYPE_I32};
        (void)vm_lib_add_native(chunk, "Task.cancel", 1, pt, VM_TYPE_BOOL, native_task_cancel);
        (void)vm_lib_add_native(chunk, "Task.status", 1, pt, VM_TYPE_STRING, native_task_status);
        (void)vm_lib_add_native(chunk, "Task.result", 1, pt, VM_TYPE_I32, native_task_result);
        (void)vm_lib_add_native(chunk, "Task.error", 1, pt, VM_TYPE_STRING, native_task_error);
        (void)vm_lib_add_native(chunk, "Task.ok", 1, pt, VM_TYPE_BOOL, native_task_ok);
    }
    {
        (void)vm_lib_add_native(chunk, "Task.step", 0, VM_NULL, VM_TYPE_I32, native_task_step);
    }
}

/* `std.debug` Native Introspection Callbacks */
/* `debug.gc()` requests a collection at the next safepoint (loop back-edge or call) */
VMReg native_dbg_gc(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    chunk->gc_pending = VM_TRUE;
    return out;
}

VMReg native_dbg_gc_count(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.i32 = (vm_i32)chunk->gc_collections;
    return out;
}

VMReg native_dbg_live_arrays(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.i32 = (vm_i32)(chunk->num_arrays - chunk->free_arrays.count);
    return out;
}

VMReg native_dbg_live_strings(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.i32 = (vm_i32)(chunk->num_strings - chunk->free_strings.count);
    return out;
}

VMReg native_dbg_func_count(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.i32 = (vm_i32)chunk->num_functions;
    return out;
}

VMReg native_dbg_bytecode_size(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.i32 = (vm_i32)chunk->code_count;
    return out;
}

VMReg native_dbg_string_count(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.i32 = (vm_i32)chunk->num_strings;
    return out;
}

VMReg native_dbg_arena_used(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.i32 = (vm_i32)chunk->string_arena_used;
    return out;
}

VMReg native_dbg_array_count(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.i32 = (vm_i32)chunk->num_arrays;
    return out;
}

VMReg native_dbg_map_count(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.i32 = (vm_i32)chunk->num_maps;
    return out;
}

VMReg native_dbg_func_name(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    vm_i32 fid = args[0].i32;
    if (fid >= 0 && (vm_u32)fid < chunk->num_functions) {
        const char *fn = chunk->functions[fid].name;
        out.str_id = vm_chunk_add_string(chunk, fn, vm_str_len(fn));
        return out;
    }
    out.str_id = vm_chunk_add_string(chunk, "<invalid>", 9);
    return out;
}

VMReg native_dbg_traceback(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.str_id = chunk->last_traceback_str_id;
    return out;
}

VMReg native_dbg_last_error(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.str_id = chunk->last_error_str_id;
    return out;
}

void vm_stdlib_register_debug(VMChunk *chunk) {
    (void)vm_lib_add_struct(chunk, "Debug", "debug");

    (void)vm_lib_add_native(chunk, "Debug.func_count", 0, VM_NULL, VM_TYPE_I32, native_dbg_func_count);
    (void)vm_lib_add_native(chunk, "Debug.gc", 0, VM_NULL, VM_TYPE_VOID, native_dbg_gc);
    (void)vm_lib_add_native(chunk, "Debug.gc_count", 0, VM_NULL, VM_TYPE_I32, native_dbg_gc_count);
    (void)vm_lib_add_native(chunk, "Debug.live_arrays", 0, VM_NULL, VM_TYPE_I32, native_dbg_live_arrays);
    (void)vm_lib_add_native(chunk, "Debug.live_strings", 0, VM_NULL, VM_TYPE_I32, native_dbg_live_strings);
    (void)vm_lib_add_native(chunk, "Debug.bytecode_size", 0, VM_NULL, VM_TYPE_I32, native_dbg_bytecode_size);
    (void)vm_lib_add_native(chunk, "Debug.string_count", 0, VM_NULL, VM_TYPE_I32, native_dbg_string_count);
    (void)vm_lib_add_native(chunk, "Debug.arena_used", 0, VM_NULL, VM_TYPE_I32, native_dbg_arena_used);
    (void)vm_lib_add_native(chunk, "Debug.array_count", 0, VM_NULL, VM_TYPE_I32, native_dbg_array_count);
    (void)vm_lib_add_native(chunk, "Debug.map_count", 0, VM_NULL, VM_TYPE_I32, native_dbg_map_count);
    (void)vm_lib_add_native(chunk, "Debug.traceback", 0, VM_NULL, VM_TYPE_STRING, native_dbg_traceback);
    (void)vm_lib_add_native(chunk, "Debug.last_error", 0, VM_NULL, VM_TYPE_STRING, native_dbg_last_error);

    {
        VMDataType pt[1] = {VM_TYPE_I32};
        (void)vm_lib_add_native(chunk, "Debug.func_name", 1, pt, VM_TYPE_STRING, native_dbg_func_name);
    }
}

/* `std.time` Native Clock Callbacks */
static VMReg native_time_now_ns(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)args; (void)argc;
    VMReg out = {0};
    out.i64 = (vm_i64)vm_sys_clock_ns();
    return out;
}

static VMReg native_time_now_ms(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)args; (void)argc;
    VMReg out = {0};
    out.f64 = (vm_f64)vm_sys_clock_ns() / 1000000.0;
    return out;
}

static VMReg native_time_now_sec(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)args; (void)argc;
    VMReg out = {0};
    out.f64 = (vm_f64)vm_sys_clock_ns() / 1000000000.0;
    return out;
}

void vm_stdlib_register_time(VMChunk *chunk) {
    (void)vm_lib_add_struct(chunk, "Time", "time");

    (void)vm_lib_add_native(chunk, "Time.now_ns", 0, VM_NULL, VM_TYPE_I64, native_time_now_ns);
    (void)vm_lib_add_native(chunk, "Time.now_ms", 0, VM_NULL, VM_TYPE_F64, native_time_now_ms);
    (void)vm_lib_add_native(chunk, "Time.now_sec", 0, VM_NULL, VM_TYPE_F64, native_time_now_sec);
}

/* `std.env` & Luau-Style `getenv` / `setenv` Native Callbacks */
static vm_u32 ensure_active_env(VMChunk *chunk) {
    if (chunk->active_env_map_id < 0 || (vm_u32)chunk->active_env_map_id >= chunk->num_maps) {
        chunk->active_env_map_id = (vm_i32)vm_chunk_map_new(chunk, VM_TYPE_STRING, VM_TYPE_STRING);
    }
    return (vm_u32)chunk->active_env_map_id;
}

static VMReg native_getenv_current(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.map_id = ensure_active_env(chunk);
    return out;
}

static VMReg native_getenv_func(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    vm_i32 fid = args[0].i32;
    if (fid >= 0 && (vm_u32)fid < chunk->num_functions && chunk->functions[fid].env_map_id >= 0) {
        out.map_id = (vm_u32)chunk->functions[fid].env_map_id;
    } else {
        out.map_id = ensure_active_env(chunk);
    }
    return out;
}

static VMReg native_getenv_key(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    vm_u32 mid = ensure_active_env(chunk);
    return vm_chunk_map_get(chunk, mid, args[0].str_id);
}

static VMReg native_env_get_default(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    vm_u32 mid = ensure_active_env(chunk);
    if (vm_chunk_map_has(chunk, mid, args[0].str_id) == VM_TRUE) {
        return vm_chunk_map_get(chunk, mid, args[0].str_id);
    }
    return args[1];
}

static VMReg native_env_has(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    vm_u32 mid = ensure_active_env(chunk);
    out.i32 = (vm_i32)vm_chunk_map_has(chunk, mid, args[0].str_id);
    return out;
}

static VMReg native_setenv_map(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    chunk->active_env_map_id = (vm_i32)args[0].map_id;
    return args[0];
}

static VMReg native_setenv_func(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    vm_i32 fid = args[0].i32;
    if (fid >= 0 && (vm_u32)fid < chunk->num_functions) {
        chunk->functions[fid].env_map_id = (vm_i32)args[1].map_id;
    }
    return args[1];
}

static VMReg native_setenv_kv(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    vm_u32 mid = ensure_active_env(chunk);
    vm_chunk_map_set(chunk, mid, args[0].str_id, args[1]);
    return args[1];
}

void vm_stdlib_register_env(VMChunk *chunk) {
    (void)vm_lib_add_struct(chunk, "Env", "env");

    /* Global Luau-style `getenv` overloads: `getenv()`, `getenv(fn)`, `getenv("key")` */
    (void)vm_lib_add_native(chunk, "getenv", 0, VM_NULL, VM_TYPE_MAP, native_getenv_current);
    {
        VMDataType pt[1] = {VM_TYPE_I32};
        (void)vm_lib_add_native(chunk, "getenv", 1, pt, VM_TYPE_MAP, native_getenv_func);
    }
    {
        VMDataType pt[1] = {VM_TYPE_STRING};
        (void)vm_lib_add_native(chunk, "getenv", 1, pt, VM_TYPE_STRING, native_getenv_key);
    }

    /* Global Luau-style `setenv` overloads: `setenv(map)`, `setenv(fn, map)`, `setenv("key", "val")` */
    {
        VMDataType pt[1] = {VM_TYPE_MAP};
        (void)vm_lib_add_native(chunk, "setenv", 1, pt, VM_TYPE_MAP, native_setenv_map);
    }
    {
        VMDataType pt[2] = {VM_TYPE_I32, VM_TYPE_MAP};
        (void)vm_lib_add_native(chunk, "setenv", 2, pt, VM_TYPE_MAP, native_setenv_func);
    }
    {
        VMDataType pt[2] = {VM_TYPE_STRING, VM_TYPE_STRING};
        (void)vm_lib_add_native(chunk, "setenv", 2, pt, VM_TYPE_STRING, native_setenv_kv);
    }

    /* `std.env` (`Env.*` / `env.*`) module methods */
    (void)vm_lib_add_native(chunk, "Env.current", 0, VM_NULL, VM_TYPE_MAP, native_getenv_current);
    {
        VMDataType pt[2] = {VM_TYPE_STRING, VM_TYPE_STRING};
        vm_u32 fid = vm_lib_add_native(chunk, "Env.get", 2, pt, VM_TYPE_STRING, native_env_get_default);
        chunk->functions[fid].min_arity = 1;
        chunk->functions[fid].param_has_default[1] = VM_TRUE;
        chunk->functions[fid].param_defaults[1].str_id = vm_chunk_add_string(chunk, "", 0);
    }
    {
        VMDataType pt[2] = {VM_TYPE_STRING, VM_TYPE_STRING};
        (void)vm_lib_add_native(chunk, "Env.set", 2, pt, VM_TYPE_STRING, native_setenv_kv);
    }
    {
        VMDataType pt[1] = {VM_TYPE_STRING};
        (void)vm_lib_add_native(chunk, "Env.has", 1, pt, VM_TYPE_BOOL, native_env_has);
    }
    {
        VMDataType pt[2] = {VM_TYPE_I32, VM_TYPE_MAP};
        (void)vm_lib_add_native(chunk, "Env.bind", 2, pt, VM_TYPE_MAP, native_setenv_func);
    }
}

/* `std.os` Native Callbacks */
VMReg native_os_platform(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    const char *p = vm_sys_platform();
    out.str_id = vm_chunk_add_string(chunk, p, vm_str_len(p));
    return out;
}

VMReg native_os_arch(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    const char *a = vm_sys_arch();
    out.str_id = vm_chunk_add_string(chunk, a, vm_str_len(a));
    return out;
}

VMReg native_os_clock(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)args; (void)argc;
    VMReg out = {0};
    out.f32 = (vm_f32)((vm_f64)vm_sys_clock_ns() / 1000000000.0);
    return out;
}

VMReg native_os_time(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)args; (void)argc;
    VMReg out = {0};
    out.i32 = (vm_i32)vm_sys_unix_time();
    return out;
}

VMReg native_os_getenv(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    const char *k = chunk->strings[args[0].str_id].chars;
    const char *v = vm_sys_getenv(k);
    out.str_id = vm_chunk_add_string(chunk, v, vm_str_len(v));
    return out;
}

void vm_stdlib_register_os(VMChunk *chunk) {
    (void)vm_lib_add_struct(chunk, "OS", "os");

    (void)vm_lib_add_native(chunk, "OS.platform", 0, VM_NULL, VM_TYPE_STRING, native_os_platform);
    (void)vm_lib_add_native(chunk, "OS.arch", 0, VM_NULL, VM_TYPE_STRING, native_os_arch);
    (void)vm_lib_add_native(chunk, "OS.clock", 0, VM_NULL, VM_TYPE_F32, native_os_clock);
    (void)vm_lib_add_native(chunk, "OS.time", 0, VM_NULL, VM_TYPE_I32, native_os_time);
    {
        VMDataType pt[1] = {VM_TYPE_STRING};
        (void)vm_lib_add_native(chunk, "OS.getenv", 1, pt, VM_TYPE_STRING, native_os_getenv);
    }
}

void vm_stdlib_register_prelude_core(VMChunk *chunk) {
    vm_stdlib_register_string_core(chunk);
    vm_stdlib_register_array_core(chunk);
    vm_stdlib_register_map_core(chunk);
}

void vm_stdlib_register_prelude(VMChunk *chunk) {
    vm_stdlib_register_string(chunk);
    vm_stdlib_register_array(chunk);
    vm_stdlib_register_map(chunk);
    vm_stdlib_register_env(chunk);
}
