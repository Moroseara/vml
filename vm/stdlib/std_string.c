#include "vm_stdlib.h"

/*
 * `std.string` & Native `string` Primitive Module (`vm/stdlib/std_string.c`)
 * Single source of truth for BOTH `String.*` / `string.*` module functions
 * AND primitive `"":method()` calls via `vm_lib_bind_primitive(chunk, VM_TYPE_STRING, "String")`!
 */

static VMReg native_str_repeat(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    vm_u32 sid = args[0].str_id;
    vm_i32 count = args[1].i32;
    if (count <= 0) {
        out.str_id = vm_chunk_add_string(chunk, "", 0);
        return out;
    }
    vm_u32 acc = vm_chunk_add_string(chunk, "", 0);
    for (vm_i32 i = 0; i < count; i++) {
        acc = vm_chunk_concat_strings(chunk, acc, sid);
    }
    out.str_id = acc;
    return out;
}

static VMReg native_str_starts_with(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    VMString s = chunk->strings[args[0].str_id];
    VMString p = chunk->strings[args[1].str_id];
    if (p.length > s.length) {
        out.i32 = (vm_i32)VM_FALSE;
        return out;
    }
    out.i32 = (vm_i32)vm_str_eq(s.chars, p.length, p.chars, p.length);
    return out;
}

static VMReg native_str_ends_with(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    VMString s = chunk->strings[args[0].str_id];
    VMString sfx = chunk->strings[args[1].str_id];
    if (sfx.length > s.length) {
        out.i32 = (vm_i32)VM_FALSE;
        return out;
    }
    out.i32 = (vm_i32)vm_str_eq(s.chars + (s.length - sfx.length), sfx.length, sfx.chars, sfx.length);
    return out;
}

/* `String.format` stays native: it accepts any argument types (coerced to string) */
static void vm_stdlib_register_string_format(VMChunk *chunk) {
    /*
     * `String.format(fmt: string, a1, ...)` & `"":format(a1, ...)`
     * Registered with `vm_lib_set_coerce_string_args(chunk, fid)` for 1..4 format arguments
     * so ANY argument type (`int`, `float`, `bool`, `string`, `array`, `map`, `struct`) is coerced!
     */
    for (vm_u8 arity = 2; arity <= 5; arity++) {
        VMDataType pt[5] = {VM_TYPE_STRING, VM_TYPE_STRING, VM_TYPE_STRING, VM_TYPE_STRING, VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "String.format", arity, arity, pt, VM_TYPE_STRING);
        vm_lib_set_coerce_string_args(chunk, fid);
        for (vm_u8 a = 1; a < arity; a++) {
            vm_chunk_emit(chunk, VM_MAKE_ABC(OP_STRFORMAT, 0, 0, a));
        }
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, arity);
    }
}

/* The `String` type bound to primitive strings; methods come from `std/string.vml` */
void vm_stdlib_register_string_core(VMChunk *chunk) {
    if (chunk->primitive_struct_name[VM_TYPE_STRING][0] != '\0') return;
    (void)vm_lib_add_struct(chunk, "String", "");   /* `string.*` aliases come from `export String as string` in the .vml */
    vm_lib_bind_primitive(chunk, VM_TYPE_STRING, "String");
    vm_stdlib_register_string_format(chunk);
}

/* Legacy compiler: every method as C */
void vm_stdlib_register_string(VMChunk *chunk) {
    if (chunk->primitive_struct_name[VM_TYPE_STRING][0] != '\0') return;

    (void)vm_lib_add_struct(chunk, "String", "string");
    vm_lib_bind_primitive(chunk, VM_TYPE_STRING, "String");

    /* `String.len(s: string): i32` & `"":len()` */
    {
        VMDataType pt[1] = {VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "String.len", 1, 1, pt, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_STRLEN, 0, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }

    /* `String.is_empty(s: string): bool` & `"":is_empty()` */
    {
        VMDataType pt[1] = {VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "String.is_empty", 1, 1, pt, VM_TYPE_BOOL);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_STRLEN, 1, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, 2, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_EQ_I32, 0, 1, 2));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 3);
    }

    /* `String.upper(s: string): string` & `"":upper()` */
    {
        VMDataType pt[1] = {VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "String.upper", 1, 1, pt, VM_TYPE_STRING);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_STRUPPER, 0, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }

    /* `String.lower(s: string): string` & `"":lower()` */
    {
        VMDataType pt[1] = {VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "String.lower", 1, 1, pt, VM_TYPE_STRING);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_STRLOWER, 0, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }

    /* `String.trim(s: string): string` & `"":trim()` */
    {
        VMDataType pt[1] = {VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "String.trim", 1, 1, pt, VM_TYPE_STRING);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_STRTRIM, 0, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }

    /* `String.sub(s: string, start: i32, count: i32): string` & `"":sub(start, count)` */
    {
        VMDataType pt[3] = {VM_TYPE_STRING, VM_TYPE_I32, VM_TYPE_I32};
        vm_u32 fid = vm_lib_begin_func(chunk, "String.sub", 3, 3, pt, VM_TYPE_STRING);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_STRSUB, 0, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 3);
    }

    /* `String.find(s: string, needle: string): i32` & `"":find(needle)` */
    {
        VMDataType pt[2] = {VM_TYPE_STRING, VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "String.find", 2, 2, pt, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_STRFIND, 0, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 2);
    }

    /* `String.contains(s: string, needle: string): bool` & `"":contains(needle)` */
    {
        VMDataType pt[2] = {VM_TYPE_STRING, VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "String.contains", 2, 2, pt, VM_TYPE_BOOL);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_STRFIND, 2, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, 3, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_GE_I32, 0, 2, 3));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 4);
    }

    /* `String.replace(s: string, old_s: string, new_s: string): string` & `"":replace(old_s, new_s)` */
    {
        VMDataType pt[3] = {VM_TYPE_STRING, VM_TYPE_STRING, VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "String.replace", 3, 3, pt, VM_TYPE_STRING);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_STRREPLACE, 0, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 3);
    }

    /* `String.repeat(s: string, count: i32): string` & `"":repeat(count)` */
    {
        VMDataType pt[2] = {VM_TYPE_STRING, VM_TYPE_I32};
        (void)vm_lib_add_native(chunk, "String.repeat", 2, pt, VM_TYPE_STRING, native_str_repeat);
    }

    /* `String.starts_with(s: string, prefix: string): bool` & `"":starts_with(prefix)` */
    {
        VMDataType pt[2] = {VM_TYPE_STRING, VM_TYPE_STRING};
        (void)vm_lib_add_native(chunk, "String.starts_with", 2, pt, VM_TYPE_BOOL, native_str_starts_with);
    }

    /* `String.ends_with(s: string, suffix: string): bool` & `"":ends_with(suffix)` */
    {
        VMDataType pt[2] = {VM_TYPE_STRING, VM_TYPE_STRING};
        (void)vm_lib_add_native(chunk, "String.ends_with", 2, pt, VM_TYPE_BOOL, native_str_ends_with);
    }

    vm_stdlib_register_string_format(chunk);
}
