#include "vm_stdlib.h"

/*
 * `std.io` & `std.coroutine` Native VM Module (`vm/stdlib/std_io.c`)
 * Registers `IO` (`std.io`) and `Coroutine` (`std.coroutine`) directly into
 * `VMChunk` using pure VM opcodes (`OP_PRINT_*`, `OP_ADD_I32`, `OP_RET`) —
 * zero `.vml` source strings!
 */

static void emit_println_overload(VMChunk *chunk, VMDataType t, VMOpCode print_op) {
    VMDataType pt[1] = {t};
    vm_u32 fid = vm_lib_begin_func(chunk, "IO.println", 1, 1, pt, VM_TYPE_VOID);
    vm_chunk_emit(chunk, VM_MAKE_ABC(print_op, 0, 0, 0));
    vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
    vm_lib_end_func(chunk, fid, 1);
}

void vm_stdlib_register_io(VMChunk *chunk) {
    (void)vm_lib_add_struct(chunk, "IO", "io");

    emit_println_overload(chunk, VM_TYPE_STRING, OP_PRINT_STR);
    emit_println_overload(chunk, VM_TYPE_I32,    OP_PRINT_I32);
    emit_println_overload(chunk, VM_TYPE_I64,    OP_PRINT_I64);
    emit_println_overload(chunk, VM_TYPE_F32,    OP_PRINT_F32);
    emit_println_overload(chunk, VM_TYPE_F64,    OP_PRINT_F64);
    emit_println_overload(chunk, VM_TYPE_BOOL,   OP_PRINT_BOOL);

    /* `struct Coroutine { pub step_id: i32 = 0 }` */
    vm_u32 cor_msid = vm_lib_add_struct(chunk, "Coroutine", "coroutine");
    VMReg zero = {0};
    vm_lib_add_struct_field(chunk, cor_msid, "step_id", VM_TYPE_I32, VM_TRUE, zero);

    /* `Coroutine.create(initial_step: i32 = 1): Coroutine` */
    {
        VMDataType pt[1] = {VM_TYPE_I32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Coroutine.create", 1, 0, pt, VM_TYPE_I32);
        chunk->functions[fid].param_has_default[0] = VM_TRUE;
        chunk->functions[fid].param_defaults[0].i32 = 1;
        /* `-2` marks `return_struct_id` to be bound to `Coroutine` when synced */
        chunk->functions[fid].return_struct_id = -2;
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }

    /* `Coroutine.resume(co: Coroutine, delta: i32 = 1): i32` */
    {
        VMDataType pt[2] = {VM_TYPE_I32, VM_TYPE_I32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Coroutine.resume", 2, 1, pt, VM_TYPE_I32);
        /* `-2` marks `param_struct_ids[0]` to be bound to `Coroutine` when synced */
        chunk->functions[fid].param_struct_ids[0] = -2;
        chunk->functions[fid].param_has_default[1] = VM_TRUE;
        chunk->functions[fid].param_defaults[1].i32 = 1;
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_ADD_I32, 0, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 2);
    }
}
