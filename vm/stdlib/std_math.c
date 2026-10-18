#include "vm_stdlib.h"

/*
 * `std.math` & `std.random` Native VM Module (`vm/stdlib/std_math.c`)
 * Registers constants, `Math`, and `Random` functions directly into `VMChunk`
 * using pure VM opcodes (`OP_MATH`, `OP_GT_*`, `OP_LT_*`, `OP_SHL_I32`, `OP_BXOR_I32`)
 * and `VMNativeFn` — zero `.vml` source strings!
 */

static VMReg native_math_pow_i32(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk;
    (void)argc;
    vm_i32 base = args[0].i32;
    vm_i32 exp  = args[1].i32;
    vm_i32 res  = 1;
    for (vm_i32 i = 0; i < exp; i++) res *= base;
    VMReg out = {0};
    out.i32 = res;
    return out;
}

static VMReg native_math_pow_f32(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk;
    (void)argc;
    vm_f32 base = args[0].f32;
    vm_i32 exp  = args[1].i32;
    vm_f32 res  = 1.0f;
    for (vm_i32 i = 0; i < exp; i++) res *= base;
    VMReg out = {0};
    out.f32 = res;
    return out;
}

static void emit_unary_math_op(VMChunk *chunk, const char *qname, VMDataType t, vm_u8 math_kind) {
    VMDataType pt[1] = {t};
    vm_u32 fid = vm_lib_begin_func(chunk, qname, 1, 1, pt, t);
    vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MATH, 0, 0, math_kind));
    vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
    vm_lib_end_func(chunk, fid, 1);
}

void vm_stdlib_register_math(VMChunk *chunk) {
    (void)vm_lib_add_struct(chunk, "Math", "math");
    (void)vm_lib_add_struct(chunk, "Random", "random");

    /* Constants: `Data = 42` and `PI = 3.14159265` */
    VMReg c_data = {0};
    c_data.i32 = 42;
    vm_lib_add_const(chunk, "math.Data", VM_TYPE_I32, c_data);
    vm_lib_add_const(chunk, "Data", VM_TYPE_I32, c_data);

    VMReg c_pi = {0};
    c_pi.f32 = 3.14159265f;
    vm_lib_add_const(chunk, "math.PI", VM_TYPE_F32, c_pi);
    vm_lib_add_const(chunk, "PI", VM_TYPE_F32, c_pi);

    /* `Math.sin(): f32` (0-arg overload returning 0.0f) */
    {
        vm_u32 fid = vm_lib_begin_func(chunk, "Math.sin", 0, 0, VM_NULL, VM_TYPE_F32);
        VMReg zf = {0};
        zf.f32 = 0.0f;
        vm_u32 k = vm_chunk_add_constant(chunk, zf, VM_TYPE_F32);
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_LOADK, 0, (vm_u16)k));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }

    /* Core `OP_MATH` intrinsics */
    emit_unary_math_op(chunk, "Math.sin",   VM_TYPE_F32, 3);
    emit_unary_math_op(chunk, "Math.cos",   VM_TYPE_F32, 4);
    emit_unary_math_op(chunk, "Math.sqrt",  VM_TYPE_F32, 0);
    emit_unary_math_op(chunk, "Math.floor", VM_TYPE_F32, 1);
    emit_unary_math_op(chunk, "Math.ceil",  VM_TYPE_F32, 2);
    emit_unary_math_op(chunk, "Math.abs",   VM_TYPE_I32, 6);
    emit_unary_math_op(chunk, "Math.abs",   VM_TYPE_F32, 5);

    /* `Math.max(a: i32, b: i32): i32` & `Math.max(a: f32, b: f32): f32` */
    {
        VMDataType pt_i[2] = {VM_TYPE_I32, VM_TYPE_I32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Math.max", 2, 2, pt_i, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_GT_I32, 2, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_JMP_FALSE, 2, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 1, 1, 0));
        vm_lib_end_func(chunk, fid, 3);
    }
    {
        VMDataType pt_f[2] = {VM_TYPE_F32, VM_TYPE_F32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Math.max", 2, 2, pt_f, VM_TYPE_F32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_GT_F32, 2, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_JMP_FALSE, 2, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 1, 1, 0));
        vm_lib_end_func(chunk, fid, 3);
    }

    /* `Math.min(a: i32, b: i32): i32` & `Math.min(a: f32, b: f32): f32` */
    {
        VMDataType pt_i[2] = {VM_TYPE_I32, VM_TYPE_I32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Math.min", 2, 2, pt_i, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_LT_I32, 2, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_JMP_FALSE, 2, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 1, 1, 0));
        vm_lib_end_func(chunk, fid, 3);
    }
    {
        VMDataType pt_f[2] = {VM_TYPE_F32, VM_TYPE_F32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Math.min", 2, 2, pt_f, VM_TYPE_F32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_LT_F32, 2, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_JMP_FALSE, 2, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 1, 1, 0));
        vm_lib_end_func(chunk, fid, 3);
    }

    /* `Math.clamp(x, lo, hi)` for `i32` and `f32` */
    {
        VMDataType pt_i[3] = {VM_TYPE_I32, VM_TYPE_I32, VM_TYPE_I32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Math.clamp", 3, 3, pt_i, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_LT_I32, 3, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_JMP_FALSE, 3, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 1, 1, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_GT_I32, 3, 0, 2));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_JMP_FALSE, 3, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 2, 1, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 4);
    }
    {
        VMDataType pt_f[3] = {VM_TYPE_F32, VM_TYPE_F32, VM_TYPE_F32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Math.clamp", 3, 3, pt_f, VM_TYPE_F32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_LT_F32, 3, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_JMP_FALSE, 3, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 1, 1, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_GT_F32, 3, 0, 2));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_JMP_FALSE, 3, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 2, 1, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 4);
    }

    /* `Math.pow(base, exp)` registered via `VMNativeFn` */
    {
        VMDataType pt_i[2] = {VM_TYPE_I32, VM_TYPE_I32};
        (void)vm_lib_add_native(chunk, "Math.pow", 2, pt_i, VM_TYPE_I32, native_math_pow_i32);
        VMDataType pt_f[2] = {VM_TYPE_F32, VM_TYPE_I32};
        (void)vm_lib_add_native(chunk, "Math.pow", 2, pt_f, VM_TYPE_F32, native_math_pow_f32);
    }

    /* `Random.next(seed: i32): i32` (Xorshift32 in pure VM instructions) */
    vm_u32 rand_next_fid;
    {
        VMDataType pt[1] = {VM_TYPE_I32};
        rand_next_fid = vm_lib_begin_func(chunk, "Random.next", 1, 1, pt, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, 1, 13));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_SHL_I32, 2, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_BXOR_I32, 0, 0, 2));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, 1, 17));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_SHR_I32, 2, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_BXOR_I32, 0, 0, 2));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, 1, 5));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_SHL_I32, 2, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_BXOR_I32, 0, 0, 2));
        VMReg mask = {0};
        mask.i32 = 2147483647;
        vm_u32 km = vm_chunk_add_constant(chunk, mask, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_LOADK, 1, (vm_u16)km));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_BAND_I32, 0, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, rand_next_fid, 3);
    }

    /* `Random.range(seed: i32, lo: i32, hi: i32): i32` */
    {
        VMDataType pt[3] = {VM_TYPE_I32, VM_TYPE_I32, VM_TYPE_I32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Random.range", 3, 3, pt, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MOVE, 4, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_CALL, 3, rand_next_fid));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MATH, 3, 3, 6)); /* abs(r) */
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_SUB_I32, 4, 2, 1)); /* hi - lo */
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MOD_I32, 3, 3, 4)); /* r % (hi - lo) */
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_ADD_I32, 0, 1, 3)); /* lo + ... */
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 5);
    }
}
