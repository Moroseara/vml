#include "vm_stdlib.h"

/*
 * `std.map` & Native `map` Primitive Module (`vm/stdlib/std_map.c`)
 * Single source of truth for BOTH `Map.*` / `map.*` module functions
 * AND primitive `{"hp": 100}:method()` calls via `vm_lib_bind_primitive(chunk, VM_TYPE_MAP, "Map")`!
 */

VMReg native_map_clear(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (args[0].map_id < chunk->num_maps) {
        chunk->maps[args[0].map_id].length = 0;
    }
    return out;
}

VMReg native_map_keys(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    out.arr_id = vm_chunk_array_new(chunk, VM_TYPE_STRING);
    if (args[0].map_id >= chunk->num_maps) return out;
    const VMMap *m = &chunk->maps[args[0].map_id];
    for (vm_u32 i = 0; i < m->length; i++) {
        VMReg k = {0};
        k.str_id = m->keys[i];
        vm_chunk_array_push(chunk, out.arr_id, k);
    }
    return out;
}

/* The `Map` type bound to primitive maps; methods come from `std/map.vml` */
void vm_stdlib_register_map_core(VMChunk *chunk) {
    if (chunk->primitive_struct_name[VM_TYPE_MAP][0] != '\0') return;
    (void)vm_lib_add_struct(chunk, "Map", "");
    vm_lib_bind_primitive(chunk, VM_TYPE_MAP, "Map");
}

/* Legacy compiler: every method as C */
void vm_stdlib_register_map(VMChunk *chunk) {
    if (chunk->primitive_struct_name[VM_TYPE_MAP][0] != '\0') return;

    (void)vm_lib_add_struct(chunk, "Map", "map");
    vm_lib_bind_primitive(chunk, VM_TYPE_MAP, "Map");

    /* `Map.new(): map` */
    {
        vm_u32 fid = vm_lib_begin_func(chunk, "Map.new", 0, 0, VM_NULL, VM_TYPE_MAP);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MAP_NEW, 0, (vm_u8)VM_TYPE_STRING, (vm_u8)VM_TYPE_I32));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }

    /* `Map.len(m: map): i32` & `m:len()` */
    {
        VMDataType pt[1] = {VM_TYPE_MAP};
        vm_u32 fid = vm_lib_begin_func(chunk, "Map.len", 1, 1, pt, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MAP_LEN, 0, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 1);
    }

    /* `Map.is_empty(m: map): bool` & `m:is_empty()` */
    {
        VMDataType pt[1] = {VM_TYPE_MAP};
        vm_u32 fid = vm_lib_begin_func(chunk, "Map.is_empty", 1, 1, pt, VM_TYPE_BOOL);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MAP_LEN, 1, 0, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, 2, 0));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_EQ_I32, 0, 1, 2));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 3);
    }

    /* `Map.has(m: map, key: string): bool` & `m:has(key)` */
    {
        VMDataType pt[2] = {VM_TYPE_MAP, VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "Map.has", 2, 2, pt, VM_TYPE_BOOL);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MAP_HAS, 0, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 2);
    }

    /* `Map.get(m: map, key: string): i32` & `m:get(key)` */
    {
        VMDataType pt[2] = {VM_TYPE_MAP, VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "Map.get", 2, 2, pt, VM_TYPE_I32);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MAP_GET, 0, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 2);
    }

    /* `Map.set` overloads (`i32`, `f32`, `string`, `bool`) */
    {
        VMDataType val_types[6] = {VM_TYPE_I32, VM_TYPE_F32, VM_TYPE_STRING, VM_TYPE_BOOL, VM_TYPE_I64, VM_TYPE_F64};
        for (int k = 0; k < 6; k++) {
            VMDataType pt[3] = {VM_TYPE_MAP, VM_TYPE_STRING, val_types[k]};
            vm_u32 fid = vm_lib_begin_func(chunk, "Map.set", 3, 3, pt, VM_TYPE_VOID);
            vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MAP_SET, 0, 1, 2));
            vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
            vm_lib_end_func(chunk, fid, 3);
        }
    }

    /* `Map.remove(m: map, key: string): bool` & `m:remove(key)` */
    {
        VMDataType pt[2] = {VM_TYPE_MAP, VM_TYPE_STRING};
        vm_u32 fid = vm_lib_begin_func(chunk, "Map.remove", 2, 2, pt, VM_TYPE_BOOL);
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_MAP_DEL, 0, 0, 1));
        vm_chunk_emit(chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
        vm_lib_end_func(chunk, fid, 2);
    }

    /* `Map.clear(m: map)` & `m:clear()` */
    {
        VMDataType pt[1] = {VM_TYPE_MAP};
        (void)vm_lib_add_native(chunk, "Map.clear", 1, pt, VM_TYPE_VOID, native_map_clear);
    }

    /* `Map.keys(m: map): array` & `m:keys()` */
    {
        VMDataType pt[1] = {VM_TYPE_MAP};
        (void)vm_lib_add_native(chunk, "Map.keys", 1, pt, VM_TYPE_ARRAY, native_map_keys);
    }
}
