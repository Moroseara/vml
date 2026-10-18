#ifndef VM_OPS_ALU_H
#define VM_OPS_ALU_H

#include "../core/vm_types.h"
#include "../core/vm_isa.h"

#define VM_INLINE static inline __attribute__((always_inline))

/* Integer Arithmetic (32-bit) */
VM_INLINE void vm_alu_add_i32(VMReg *R, VMInst inst)     { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 + R[VM_GET_C(inst)].i32; }
VM_INLINE void vm_alu_sub_i32(VMReg *R, VMInst inst)     { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 - R[VM_GET_C(inst)].i32; }
VM_INLINE void vm_alu_mul_i32(VMReg *R, VMInst inst)     { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 * R[VM_GET_C(inst)].i32; }
/* Division helpers: the divisor is checked for 0 by the caller; -1 wraps instead of trapping on MIN / -1 */
VM_INLINE vm_i32 vm_div_i32(vm_i32 a, vm_i32 b) { return (b == -1) ? (vm_i32)(0u - (vm_u32)a) : a / b; }
VM_INLINE vm_i32 vm_mod_i32(vm_i32 a, vm_i32 b) { return (b == -1) ? 0 : a % b; }
VM_INLINE vm_i64 vm_div_i64(vm_i64 a, vm_i64 b) { return (b == -1) ? (vm_i64)(0ull - (vm_u64)a) : a / b; }
VM_INLINE vm_i64 vm_mod_i64(vm_i64 a, vm_i64 b) { return (b == -1) ? 0 : a % b; }
VM_INLINE void vm_alu_add_imm_i32(VMReg *R, VMInst inst) { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 + (vm_i8)VM_GET_C(inst); }

/* Bitwise Operations (32-bit) */
VM_INLINE void vm_alu_band_i32(VMReg *R, VMInst inst)    { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 & R[VM_GET_C(inst)].i32; }
VM_INLINE void vm_alu_bor_i32(VMReg *R, VMInst inst)     { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 | R[VM_GET_C(inst)].i32; }
VM_INLINE void vm_alu_bxor_i32(VMReg *R, VMInst inst)    { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 ^ R[VM_GET_C(inst)].i32; }
VM_INLINE void vm_alu_bnot_i32(VMReg *R, VMInst inst)    { R[VM_GET_A(inst)].i32 = ~R[VM_GET_B(inst)].i32; }
VM_INLINE void vm_alu_shl_i32(VMReg *R, VMInst inst)     { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 << (R[VM_GET_C(inst)].i32 & 31); }
VM_INLINE void vm_alu_shr_i32(VMReg *R, VMInst inst)     { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 >> (R[VM_GET_C(inst)].i32 & 31); }

/* 64-bit Integer Arithmetic */
VM_INLINE void vm_alu_add_i64(VMReg *R, VMInst inst)     { R[VM_GET_A(inst)].i64 = R[VM_GET_B(inst)].i64 + R[VM_GET_C(inst)].i64; }
VM_INLINE void vm_alu_sub_i64(VMReg *R, VMInst inst)     { R[VM_GET_A(inst)].i64 = R[VM_GET_B(inst)].i64 - R[VM_GET_C(inst)].i64; }
VM_INLINE void vm_alu_mul_i64(VMReg *R, VMInst inst)     { R[VM_GET_A(inst)].i64 = R[VM_GET_B(inst)].i64 * R[VM_GET_C(inst)].i64; }

#endif /* VM_OPS_ALU_H */
