#ifndef VM_CORE_BOOL_H
#define VM_CORE_BOOL_H

/*
 * ============================================================================
 * VM KERNEL BOOLEAN PRIMITIVE (`vm/core/vm_bool.h`)
 * Owned strictly by the VM Kernel (Zero <stdbool.h> dependency) and bridged
 * to all VM subsystems and the Compiler.
 * ============================================================================
 */

typedef unsigned char vm_bool;

#define VM_FALSE ((vm_bool)0)
#define VM_TRUE  ((vm_bool)1)

static inline vm_bool vm_bool_from_int(int cond) {
    return (cond != 0) ? VM_TRUE : VM_FALSE;
}

static inline vm_bool vm_bool_not(vm_bool b) {
    return (b == VM_FALSE) ? VM_TRUE : VM_FALSE;
}

#endif /* VM_CORE_BOOL_H */
