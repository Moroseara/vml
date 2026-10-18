#ifndef VM_OPS_CMP_H
#define VM_OPS_CMP_H

#include "../core/vm_types.h"
#include "../core/vm_isa.h"

#define VM_CMP_INLINE static inline __attribute__((always_inline))

VM_CMP_INLINE void vm_cmp_eq_i32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i32 == R[VM_GET_C(inst)].i32); }
VM_CMP_INLINE void vm_cmp_ne_i32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i32 != R[VM_GET_C(inst)].i32); }
VM_CMP_INLINE void vm_cmp_lt_i32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i32 <  R[VM_GET_C(inst)].i32); }
VM_CMP_INLINE void vm_cmp_le_i32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i32 <= R[VM_GET_C(inst)].i32); }
VM_CMP_INLINE void vm_cmp_gt_i32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i32 >  R[VM_GET_C(inst)].i32); }
VM_CMP_INLINE void vm_cmp_ge_i32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i32 >= R[VM_GET_C(inst)].i32); }

VM_CMP_INLINE void vm_cmp_eq_f32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f32 == R[VM_GET_C(inst)].f32); }
VM_CMP_INLINE void vm_cmp_ne_f32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f32 != R[VM_GET_C(inst)].f32); }
VM_CMP_INLINE void vm_cmp_lt_f32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f32 <  R[VM_GET_C(inst)].f32); }
VM_CMP_INLINE void vm_cmp_le_f32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f32 <= R[VM_GET_C(inst)].f32); }
VM_CMP_INLINE void vm_cmp_gt_f32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f32 >  R[VM_GET_C(inst)].f32); }
VM_CMP_INLINE void vm_cmp_ge_f32(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f32 >= R[VM_GET_C(inst)].f32); }

VM_CMP_INLINE void vm_cmp_eq_i64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i64 == R[VM_GET_C(inst)].i64); }
VM_CMP_INLINE void vm_cmp_ne_i64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i64 != R[VM_GET_C(inst)].i64); }
VM_CMP_INLINE void vm_cmp_lt_i64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i64 <  R[VM_GET_C(inst)].i64); }
VM_CMP_INLINE void vm_cmp_le_i64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i64 <= R[VM_GET_C(inst)].i64); }
VM_CMP_INLINE void vm_cmp_gt_i64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i64 >  R[VM_GET_C(inst)].i64); }
VM_CMP_INLINE void vm_cmp_ge_i64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].i64 >= R[VM_GET_C(inst)].i64); }

VM_CMP_INLINE void vm_cmp_eq_f64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f64 == R[VM_GET_C(inst)].f64); }
VM_CMP_INLINE void vm_cmp_ne_f64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f64 != R[VM_GET_C(inst)].f64); }
VM_CMP_INLINE void vm_cmp_lt_f64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f64 <  R[VM_GET_C(inst)].f64); }
VM_CMP_INLINE void vm_cmp_le_f64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f64 <= R[VM_GET_C(inst)].f64); }
VM_CMP_INLINE void vm_cmp_gt_f64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f64 >  R[VM_GET_C(inst)].f64); }
VM_CMP_INLINE void vm_cmp_ge_f64(VMReg *R, VMInst inst)  { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].f64 >= R[VM_GET_C(inst)].f64); }

VM_CMP_INLINE void vm_logic_and(VMReg *R, VMInst inst)   { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].b == VM_TRUE && R[VM_GET_C(inst)].b == VM_TRUE); }
VM_CMP_INLINE void vm_logic_or(VMReg *R, VMInst inst)    { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(R[VM_GET_B(inst)].b == VM_TRUE || R[VM_GET_C(inst)].b == VM_TRUE); }
VM_CMP_INLINE void vm_logic_not(VMReg *R, VMInst inst)   { R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_not(R[VM_GET_B(inst)].b); }

#endif /* VM_OPS_CMP_H */
