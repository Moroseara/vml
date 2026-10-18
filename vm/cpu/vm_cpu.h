#ifndef VM_CPU_H
#define VM_CPU_H

#include "../core/vm_types.h"
#include "../memory/vm_memory.h"

/*
 * ============================================================================
 * VM KERNEL VIRTUAL CPU (`vm/cpu/vm_cpu.h`)
 * Register-Window Call Stack, Direct-Threaded Execution Engine, and Optional
 * Sandbox Fuel Limit (`max_instructions`) for Web Playgrounds & Safe Embeds.
 * ============================================================================
 */

#define VM_MAX_REGISTERS   4096
#define VM_MAX_CALL_FRAMES 256
#define VM_MAX_TRAPS       64

typedef struct {
    vm_u32 return_ip;
    vm_u32 base_reg;
    vm_u32 arena_mark;
    vm_u32 num_strings_mark;
    vm_i32 prev_env_map_id;
    vm_i32 prev_func_id;
    vm_i32 prev_closure;
    vm_u8  ret_reg;
    vm_u16 func_id;
} VMCallFrame;

typedef struct {
    vm_u32 catch_ip;
    vm_u32 frame_depth;
    vm_u32 base_reg;
    vm_u8  err_reg;
} VMTrapFrame;

typedef struct {
    VMReg       registers[VM_MAX_REGISTERS];
    VMCallFrame frames[VM_MAX_CALL_FRAMES];
    vm_u32      frame_count;
    VMTrapFrame traps[VM_MAX_TRAPS];
    vm_u32      trap_count;
    vm_bool     catch_top_level;
    vm_bool     has_error;
    vm_u32      last_error_str_id;
    vm_u64      max_instructions; /* 0 = unlimited, >0 = fuel budget for Web Playground */
    vm_u64      executed_loops;
    vm_i32      cur_closure;      /* closure of the running function (-1 = plain function) */
} VMCPU;

void  vm_cpu_init(VMCPU *cpu);
VMReg vm_cpu_execute(VMCPU *cpu, VMChunk *chunk);

#endif /* VM_CPU_H */
