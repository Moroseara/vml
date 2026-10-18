#include "vm_stdlib.h"

/*
 * `std.bit` Native VM Module (`vm/stdlib/std_bit.c`)
 * Registers `struct Bit` (`std.bit`) directly into `VMChunk` using pure
 * VM bitwise opcodes (`OP_BAND_I32`, `OP_BOR_I32`, `OP_BXOR_I32`, `OP_BNOT_I32`,
 * `OP_SHL_I32`, `OP_SHR_I32`, `OP_MATH`) — zero `.vml` source strings!
 */

static void emit_bin_bit_op(VMChunk *chunk, const char *qname, VMOpCode op) {
    VMDataType pt[2] = {VM_TYPE_I32, VM_TYPE_I32};
    vm_u32 fid = vm_lib_begin_func(chunk, qname, 2, 2, pt, VM_TYPE_I32);
    vm_chunk_emit(chunk, VM_MAKE_ABC(op, 0, 0, 1));
    vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
    vm_lib_end_func(chunk, fid, 2);
}

void vm_stdlib_register_bit(VMChunk *chunk) {
    (void)vm_lib_add_struct(chunk, "Bit", "bit");

    emit_bin_bit_op(chunk, "Bit.band", OP_BAND_I32);
    emit_bin_bit_op(chunk, "Bit.bor",  OP_BOR_I32);
    emit_bin_bit_op(chunk, "Bit.bxor", OP_BXOR_I32);
    emit_bin_bit_op(chunk, "Bit.shl",  OP_SHL_I32);
    emit_bin_bit_op(chunk, "Bit.shr",  OP_SHR_I32);

    /* `Bit.bnot(a: i32): i32` */
    {
        VMDataType pt[1] = {VM_TYPE_I32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Bit.bnot", 1, 1, pt, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_BNOT_I32, 0, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }

    /* `Bit.count_ones(x: i32): i32` */
    {
        VMDataType pt[1] = {VM_TYPE_I32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Bit.count_ones", 1, 1, pt, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MATH, 0, 0, 7));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }
}
