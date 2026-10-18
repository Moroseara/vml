#ifndef VM_CORE_TYPES_H
#define VM_CORE_TYPES_H

#include "vm_bool.h"

/*
 * ============================================================================
 * VM KERNEL PRIMITIVE TYPES (`vm/core/vm_types.h`)
 * Zero C Standard Library Header Dependencies.
 * ============================================================================
 */

typedef unsigned char      vm_u8;
typedef signed char        vm_i8;
typedef unsigned short     vm_u16;
typedef signed short       vm_i16;
typedef unsigned int       vm_u32;
typedef signed int         vm_i32;
typedef unsigned long long vm_u64;
typedef signed long long   vm_i64;
typedef float              vm_f32;
typedef double             vm_f64;
typedef unsigned long      vm_size;

#define VM_NULL ((void *)0)

/* VM Kernel Static Type System */
typedef enum {
    VM_TYPE_VOID = 0,
    VM_TYPE_I32,
    VM_TYPE_I64,
    VM_TYPE_F32,    /* `f32` and `float` */
    VM_TYPE_F64,    /* `f64` */
    VM_TYPE_BOOL,   /* `bool` -> backed by `vm_bool` */
    VM_TYPE_STRING, /* `string` -> backed by `VMString` */
    VM_TYPE_ARRAY,  /* `array` / `T[]` -> backed by `VMArray` */
    VM_TYPE_MAP,    /* `map` / `{K: V}` -> backed by `VMMap` */
    VM_TYPE_COUNT
} VMDataType;

/* 64-bit Unboxed Virtual CPU Register */
typedef union {
    vm_i32  i32;
    vm_i64  i64;
    vm_f32  f32;
    vm_f64  f64;
    vm_bool b;
    vm_u32  str_id;
    vm_u32  arr_id;
    vm_u32  map_id;
    vm_u64  raw;
} VMReg;

const char *vm_type_name(VMDataType type);

#endif /* VM_CORE_TYPES_H */
