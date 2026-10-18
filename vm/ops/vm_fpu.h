#ifndef VM_OPS_FPU_H
#define VM_OPS_FPU_H

#include "../core/vm_types.h"
#include "../core/vm_isa.h"

#define VM_FPU_INLINE static inline __attribute__((always_inline))

VM_FPU_INLINE void vm_fpu_add_f32(VMReg *R, VMInst inst) { R[VM_GET_A(inst)].f32 = R[VM_GET_B(inst)].f32 + R[VM_GET_C(inst)].f32; }
VM_FPU_INLINE void vm_fpu_sub_f32(VMReg *R, VMInst inst) { R[VM_GET_A(inst)].f32 = R[VM_GET_B(inst)].f32 - R[VM_GET_C(inst)].f32; }
VM_FPU_INLINE void vm_fpu_mul_f32(VMReg *R, VMInst inst) { R[VM_GET_A(inst)].f32 = R[VM_GET_B(inst)].f32 * R[VM_GET_C(inst)].f32; }
VM_FPU_INLINE void vm_fpu_div_f32(VMReg *R, VMInst inst) { R[VM_GET_A(inst)].f32 = R[VM_GET_B(inst)].f32 / R[VM_GET_C(inst)].f32; }

VM_FPU_INLINE void vm_fpu_add_f64(VMReg *R, VMInst inst) { R[VM_GET_A(inst)].f64 = R[VM_GET_B(inst)].f64 + R[VM_GET_C(inst)].f64; }
VM_FPU_INLINE void vm_fpu_sub_f64(VMReg *R, VMInst inst) { R[VM_GET_A(inst)].f64 = R[VM_GET_B(inst)].f64 - R[VM_GET_C(inst)].f64; }
VM_FPU_INLINE void vm_fpu_mul_f64(VMReg *R, VMInst inst) { R[VM_GET_A(inst)].f64 = R[VM_GET_B(inst)].f64 * R[VM_GET_C(inst)].f64; }
VM_FPU_INLINE void vm_fpu_div_f64(VMReg *R, VMInst inst) { R[VM_GET_A(inst)].f64 = R[VM_GET_B(inst)].f64 / R[VM_GET_C(inst)].f64; }

#endif /* VM_OPS_FPU_H */
