#ifndef VM_INTRINSICS_H
#define VM_INTRINSICS_H

#include "../core/vm_types.h"
#include "../core/vm_isa.h"
#include "../memory/vm_memory.h"

/*
 * Intrinsics: the small set of primitive operations the `.vml` standard library is built on.
 *
 * A stdlib module declares the public signature and binds it to an intrinsic:
 *
 *     @[intrinsic("str_upper")] pub func String.upper(self): string
 *
 * An intrinsic is either a short opcode sequence (the declared function's body becomes
 * `ops...; RET 0`), or a native C function. Native intrinsics live in functions named
 * `$<name>` so `.vmb` images re-bind them by name; the declared name is an alias.
 * Only embedded stdlib modules may use `@[intrinsic]`.
 */

#define VM_INTRINSIC_MAX_OPS 3

typedef struct VMIntrinsicS {
    const char *name;
    vm_u8       arity;
    vm_u8       num_ops;
    VMInst      ops[VM_INTRINSIC_MAX_OPS];  /* run with the arguments in R[0..arity-1]; result in R[0] */
    vm_bool     c_is_imm;                   /* operand C of the ops is an immediate, not a register */
    VMNativeFn  native;                     /* used when num_ops == 0 */
} VMIntrinsic;

const VMIntrinsic *vm_intrinsic_find(const char *name, vm_u32 len);
vm_u32             vm_intrinsic_count(void);
const VMIntrinsic *vm_intrinsic_at(vm_u32 i);

/* Registers every native intrinsic as `$<name>` (used to re-bind natives of loaded images) */
void vm_intrinsics_register_natives(VMChunk *chunk);

#endif /* VM_INTRINSICS_H */
