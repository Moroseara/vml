#include "vm_stdlib.h"

/*
 * `std.array` & Native `array` Primitive Module (`vm/stdlib/std_array.c`)
 * Single source of truth for BOTH `Array.*` / `array.*` module functions
 * AND primitive `[10, 20, 30]:method()` calls via `vm_lib_bind_primitive(chunk, VM_TYPE_ARRAY, "Array")`!
 */

static VMReg native_arr_clear(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (args[0].arr_id < chunk->num_arrays) {
        chunk->arrays[args[0].arr_id].length = 0;
    }
    return out;
}

static VMReg native_arr_contains_i32(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (args[0].arr_id >= chunk->num_arrays) return out;
    const VMArray *a = &chunk->arrays[args[0].arr_id];
    for (vm_u32 i = 0; i < a->length; i++) {
        if (a->items[i].i32 == args[1].i32) {
            out.i32 = (vm_i32)VM_TRUE;
            return out;
        }
    }
    out.i32 = (vm_i32)VM_FALSE;
    return out;
}

static VMReg native_arr_contains_i64(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (args[0].arr_id >= chunk->num_arrays) return out;
    const VMArray *a = &chunk->arrays[args[0].arr_id];
    for (vm_u32 i = 0; i < a->length; i++) {
        if (a->items[i].i64 == args[1].i64) {
            out.i32 = (vm_i32)VM_TRUE;
            return out;
        }
    }
    out.i32 = (vm_i32)VM_FALSE;
    return out;
}

static VMReg native_arr_contains_str(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (args[0].arr_id >= chunk->num_arrays) return out;
    const VMArray *a = &chunk->arrays[args[0].arr_id];
    for (vm_u32 i = 0; i < a->length; i++) {
        if (vm_chunk_str_cmp(chunk, a->items[i].str_id, args[1].str_id) == 0) {
            out.i32 = (vm_i32)VM_TRUE;
            return out;
        }
    }
    out.i32 = (vm_i32)VM_FALSE;
    return out;
}

static VMReg native_arr_reverse(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = args[0];
    if (args[0].arr_id >= chunk->num_arrays) return out;
    VMArray *a = &chunk->arrays[args[0].arr_id];
    for (vm_u32 i = 0; i < a->length / 2; i++) {
        vm_u32 j = a->length - 1 - i;
        VMReg tmp = a->items[i];
        a->items[i] = a->items[j];
        a->items[j] = tmp;
    }
    return out;
}

static VMReg native_arr_join(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (args[0].arr_id >= chunk->num_arrays) {
        out.str_id = vm_chunk_add_string(chunk, "", 0);
        return out;
    }
    VMArray *a = &chunk->arrays[args[0].arr_id];
    vm_u32 sep_sid = args[1].str_id;
    vm_u32 acc = vm_chunk_add_string(chunk, "", 0);
    vm_u8 epair = (vm_u8)(((vm_u8)a->elem_type << 4) | ((vm_u8)VM_TYPE_STRING & 0x0Fu));
    for (vm_u32 i = 0; i < a->length; i++) {
        if (i > 0) acc = vm_chunk_concat_strings(chunk, acc, sep_sid);
        VMReg es = vm_chunk_cast_value(chunk, a->items[i], epair);
        acc = vm_chunk_concat_strings(chunk, acc, es.str_id);
    }
    out.str_id = acc;
    return out;
}

/* The `Array` type bound to primitive arrays; methods come from `std/array.vml` */
void vm_stdlib_register_array_core(VMChunk *chunk) {
    if (chunk->primitive_struct_name[VM_TYPE_ARRAY][0] != '\0') return;
    (void)vm_lib_add_struct(chunk, "Array", "");   /* `array.*` aliases come from `export Array as array` in the .vml */
    vm_lib_bind_primitive(chunk, VM_TYPE_ARRAY, "Array");
}

/* Legacy compiler: every method as C */
void vm_stdlib_register_array(VMChunk *chunk) {
    if (chunk->primitive_struct_name[VM_TYPE_ARRAY][0] != '\0') return;

    (void)vm_lib_add_struct(chunk, "Array", "array");
    vm_lib_bind_primitive(chunk, VM_TYPE_ARRAY, "Array");

    /* `Array.len(a: array): i32` & `arr:len()` */
    {
        VMDataType pt[1] = {VM_TYPE_ARRAY};
        vm_u32 fid = vm_lib_begin_func(chunk, "Array.len", 1, 1, pt, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_ARR_LEN, 0, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }

    /* `Array.is_empty(a: array): bool` & `arr:is_empty()` */
    {
        VMDataType pt[1] = {VM_TYPE_ARRAY};
        vm_u32 fid = vm_lib_begin_func(chunk, "Array.is_empty", 1, 1, pt, VM_TYPE_BOOL);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_ARR_LEN, 1, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, 2, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_EQ_I32, 0, 1, 2));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 3);
    }

    /* `Array.push` overloads (`i32`, `f32`, `string`, `bool`) -> returns new length */
    {
        VMDataType elem_types[6] = {VM_TYPE_I32, VM_TYPE_F32, VM_TYPE_STRING, VM_TYPE_BOOL, VM_TYPE_I64, VM_TYPE_F64};
        for (int k = 0; k < 6; k++) {
            VMDataType pt[2] = {VM_TYPE_ARRAY, elem_types[k]};
            vm_u32 fid = vm_lib_begin_func(chunk, "Array.push", 2, 2, pt, VM_TYPE_I32);
            vm_chunk_emit(chunk, VM_MAKE_ABC(OP_ARR_PUSH, 0, 1, 0));
            vm_chunk_emit(chunk, VM_MAKE_ABC(OP_ARR_LEN, 0, 0, 0));
            vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
            vm_lib_end_func(chunk, fid, 2);
        }
    }

    /* `Array.pop(a: array): i32` & `arr:pop()` */
    {
        VMDataType pt[1] = {VM_TYPE_ARRAY};
        vm_u32 fid = vm_lib_begin_func(chunk, "Array.pop", 1, 1, pt, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_ARR_POP, 0, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }

    /* `Array.get(a: array, idx: i32): i32` & `arr:get(idx)` */
    {
        VMDataType pt[2] = {VM_TYPE_ARRAY, VM_TYPE_I32};
        vm_u32 fid = vm_lib_begin_func(chunk, "Array.get", 2, 2, pt, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_ARR_GET, 0, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 2);
    }

    /* `Array.set` overloads (`i32`, `f32`, `string`, `bool`) */
    {
        VMDataType elem_types[6] = {VM_TYPE_I32, VM_TYPE_F32, VM_TYPE_STRING, VM_TYPE_BOOL, VM_TYPE_I64, VM_TYPE_F64};
        for (int k = 0; k < 6; k++) {
            VMDataType pt[3] = {VM_TYPE_ARRAY, VM_TYPE_I32, elem_types[k]};
            vm_u32 fid = vm_lib_begin_func(chunk, "Array.set", 3, 3, pt, VM_TYPE_VOID);
            vm_chunk_emit(chunk, VM_MAKE_ABC(OP_ARR_SET, 0, 1, 2));
            vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
            vm_lib_end_func(chunk, fid, 3);
        }
    }

    /* `Array.clear(a: array)` & `arr:clear()` */
    {
        VMDataType pt[1] = {VM_TYPE_ARRAY};
        (void)vm_lib_add_native(chunk, "Array.clear", 1, pt, VM_TYPE_VOID, native_arr_clear);
    }

    /* `Array.contains(a: array, val)` & `arr:contains(val)` */
    {
        VMDataType pt_i[2] = {VM_TYPE_ARRAY, VM_TYPE_I32};
        (void)vm_lib_add_native(chunk, "Array.contains", 2, pt_i, VM_TYPE_BOOL, native_arr_contains_i32);

        VMDataType pt_l[2] = {VM_TYPE_ARRAY, VM_TYPE_I64};
        (void)vm_lib_add_native(chunk, "Array.contains", 2, pt_l, VM_TYPE_BOOL, native_arr_contains_i64);

        VMDataType pt_s[2] = {VM_TYPE_ARRAY, VM_TYPE_STRING};
        (void)vm_lib_add_native(chunk, "Array.contains", 2, pt_s, VM_TYPE_BOOL, native_arr_contains_str);
    }

    /* `Array.reverse(a: array): array` & `arr:reverse()` */
    {
        VMDataType pt[1] = {VM_TYPE_ARRAY};
        (void)vm_lib_add_native(chunk, "Array.reverse", 1, pt, VM_TYPE_ARRAY, native_arr_reverse);
    }

    /* `Array.join(a: array, sep: string = ", "): string` & `arr:join(sep)` */
    {
        VMDataType pt[2] = {VM_TYPE_ARRAY, VM_TYPE_STRING};
        vm_u32 fid = vm_lib_add_native(chunk, "Array.join", 2, pt, VM_TYPE_STRING, native_arr_join);
        chunk->functions[fid].min_arity = 1;
        chunk->functions[fid].param_has_default[1] = VM_TRUE;
        chunk->functions[fid].param_defaults[1].str_id = vm_chunk_add_string(chunk, ", ", 2);
    }
}
