#ifndef VM_STRING_H
#define VM_STRING_H

#include "../core/vm_types.h"

/*
 * ============================================================================
 * VM KERNEL STRING SUBSYSTEM (`vm/string/vm_string.h`)
 * Pure VM String slice, zero-dependency string manipulation & number parsing.
 * ============================================================================
 */

typedef struct {
    const char *chars;
    vm_u32      length;
} VMString;

vm_u32  vm_str_len(const char *s);
vm_bool vm_str_eq(const char *a, vm_u32 alen, const char *b, vm_u32 blen);
vm_bool vm_str_eq_cstr(const char *a, vm_u32 alen, const char *cstr);
void    vm_str_copy(char *dst, const char *src, vm_u32 len, vm_u32 max_len);
void    vm_str_join_dot(char *dst, vm_u32 max_len,
                        const char *prefix, vm_u32 plen,
                        const char *name, vm_u32 nlen);

/* String <-> Number Conversion Utilities for `as` Type Casting (`OP_CAST`) */
vm_i64  vm_str_parse_i64(const char *s, vm_u32 len);
vm_f64  vm_str_parse_f64(const char *s, vm_u32 len);
vm_u32  vm_i64_to_str(char *dst, vm_i64 val);
/* Shortest text that reads back as the same value: "0.1", "2.5", "1e+20", whole numbers as "3".
   is_f32: shortest for float precision. out needs 32 bytes. Returns the length. */
vm_u32  vm_f64_to_str(char *out, vm_f64 v, vm_bool is_f32);

#endif /* VM_STRING_H */
