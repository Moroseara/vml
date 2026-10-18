#include "vm_cpu.h"
#include "../ops/vm_alu.h"
#include "../ops/vm_fpu.h"
#include "../ops/vm_cmp.h"
#include "../syscall/vm_syscall.h"
#include "../gc/vm_gc.h"

void vm_cpu_init(VMCPU *cpu) {
    cpu->frame_count = 0;
    cpu->trap_count = 0;
    cpu->catch_top_level = VM_FALSE;
    cpu->has_error = VM_FALSE;
    cpu->last_error_str_id = 0;
    cpu->max_instructions = 0;
    cpu->executed_loops = 0;
    cpu->cur_closure = -1;
}

static VMReg execute(VMCPU *cpu, VMChunk *chunk);

VMReg vm_cpu_execute(VMCPU *cpu, VMChunk *chunk) {
    vm_gc_seal(chunk);
    vm_gc_push_cpu(chunk, cpu);
    VMReg r = execute(cpu, chunk);
    vm_gc_pop_cpu(chunk);
    return r;
}

static VMReg execute(VMCPU *cpu, VMChunk *chunk) {
    vm_u8 trap_kind = 0;
    vm_u32 trap_msg = 0;
    vm_i32 trap_level = 0;
    static const void * const dispatch_table[OP_COUNT] = {
        [OP_HALT] = &&L_HALT, [OP_LOADK] = &&L_LOADK,
        [OP_LOAD_IMM_I32] = &&L_LOAD_IMM_I32, [OP_LOAD_BOOL] = &&L_LOAD_BOOL, [OP_MOVE] = &&L_MOVE,
        [OP_ADD_I32] = &&L_ADD_I32, [OP_SUB_I32] = &&L_SUB_I32, [OP_MUL_I32] = &&L_MUL_I32,
        [OP_DIV_I32] = &&L_DIV_I32, [OP_MOD_I32] = &&L_MOD_I32, [OP_ADD_IMM_I32] = &&L_ADD_IMM_I32,
        [OP_BAND_I32] = &&L_BAND_I32, [OP_BOR_I32] = &&L_BOR_I32, [OP_BXOR_I32] = &&L_BXOR_I32,
        [OP_BNOT_I32] = &&L_BNOT_I32, [OP_SHL_I32] = &&L_SHL_I32, [OP_SHR_I32] = &&L_SHR_I32,
        [OP_MOD_I64] = &&L_MOD_I64, [OP_ADD_IMM_I64] = &&L_ADD_IMM_I64, [OP_LOAD_IMM_I64] = &&L_LOAD_IMM_I64, [OP_BAND_I64] = &&L_BAND_I64, [OP_BOR_I64] = &&L_BOR_I64, [OP_BXOR_I64] = &&L_BXOR_I64, [OP_BNOT_I64] = &&L_BNOT_I64, [OP_SHL_I64] = &&L_SHL_I64, [OP_SHR_I64] = &&L_SHR_I64, [OP_I32_TO_I64] = &&L_I32_TO_I64, [OP_I64_TO_I32] = &&L_I64_TO_I32, [OP_JMP_NZ_I64] = &&L_JMP_NZ_I64, [OP_FORLOOP_I64] = &&L_FORLOOP_I64, [OP_EQ_I64_JF] = &&L_EQ_I64_JF, [OP_NE_I64_JF] = &&L_NE_I64_JF, [OP_LT_I64_JF] = &&L_LT_I64_JF, [OP_LE_I64_JF] = &&L_LE_I64_JF, [OP_GT_I64_JF] = &&L_GT_I64_JF, [OP_GE_I64_JF] = &&L_GE_I64_JF, [OP_LI_EQ_I64_JF] = &&L_LI_EQ_I64_JF, [OP_LI_NE_I64_JF] = &&L_LI_NE_I64_JF, [OP_LI_LT_I64_JF] = &&L_LI_LT_I64_JF, [OP_LI_LE_I64_JF] = &&L_LI_LE_I64_JF, [OP_LI_GT_I64_JF] = &&L_LI_GT_I64_JF, [OP_LI_GE_I64_JF] = &&L_LI_GE_I64_JF, [OP_ADD_IMM_I64_MV] = &&L_ADD_IMM_I64_MV, [OP_SUB_I64_MV] = &&L_SUB_I64_MV,
        [OP_ADD_I64] = &&L_ADD_I64, [OP_SUB_I64] = &&L_SUB_I64, [OP_MUL_I64] = &&L_MUL_I64, [OP_DIV_I64] = &&L_DIV_I64,
        [OP_ADD_F32] = &&L_ADD_F32, [OP_SUB_F32] = &&L_SUB_F32, [OP_MUL_F32] = &&L_MUL_F32, [OP_DIV_F32] = &&L_DIV_F32,
        [OP_ADD_F64] = &&L_ADD_F64, [OP_SUB_F64] = &&L_SUB_F64, [OP_MUL_F64] = &&L_MUL_F64, [OP_DIV_F64] = &&L_DIV_F64,
        [OP_EQ_I32] = &&L_EQ_I32, [OP_NE_I32] = &&L_NE_I32, [OP_LT_I32] = &&L_LT_I32,
        [OP_LE_I32] = &&L_LE_I32, [OP_GT_I32] = &&L_GT_I32, [OP_GE_I32] = &&L_GE_I32,
        [OP_EQ_F32] = &&L_EQ_F32, [OP_NE_F32] = &&L_NE_F32, [OP_LT_F32] = &&L_LT_F32,
        [OP_LE_F32] = &&L_LE_F32, [OP_GT_F32] = &&L_GT_F32, [OP_GE_F32] = &&L_GE_F32,
        [OP_EQ_I64] = &&L_EQ_I64, [OP_NE_I64] = &&L_NE_I64, [OP_LT_I64] = &&L_LT_I64,
        [OP_LE_I64] = &&L_LE_I64, [OP_GT_I64] = &&L_GT_I64, [OP_GE_I64] = &&L_GE_I64,
        [OP_EQ_F64] = &&L_EQ_F64, [OP_NE_F64] = &&L_NE_F64, [OP_LT_F64] = &&L_LT_F64,
        [OP_LE_F64] = &&L_LE_F64, [OP_GT_F64] = &&L_GT_F64, [OP_GE_F64] = &&L_GE_F64,
        [OP_EQ_STR] = &&L_EQ_STR, [OP_NE_STR] = &&L_NE_STR, [OP_LT_STR] = &&L_LT_STR,
        [OP_LE_STR] = &&L_LE_STR, [OP_GT_STR] = &&L_GT_STR, [OP_GE_STR] = &&L_GE_STR,
        [OP_AND_BOOL] = &&L_AND_BOOL, [OP_OR_BOOL] = &&L_OR_BOOL, [OP_NOT_BOOL] = &&L_NOT_BOOL,
        [OP_JMP] = &&L_JMP, [OP_JMP_FALSE] = &&L_JMP_FALSE, [OP_JMP_TRUE] = &&L_JMP_TRUE,
        [OP_JMP_NZ_I32] = &&L_JMP_NZ_I32, [OP_FORLOOP_I32] = &&L_FORLOOP_I32,
        [OP_CALL] = &&L_CALL, [OP_CALL_REG] = &&L_CALL_REG, [OP_RET] = &&L_RET,
        [OP_GET_OUTER] = &&L_GET_OUTER,
        [OP_CLOSURE] = &&L_CLOSURE, [OP_CELL_NEW] = &&L_CELL_NEW, [OP_CELL_GET] = &&L_CELL_GET,
        [OP_CELL_SET] = &&L_CELL_SET, [OP_UPVAL_GET] = &&L_UPVAL_GET, [OP_UPVAL_SET] = &&L_UPVAL_SET, [OP_TRAP] = &&L_TRAP,
        [OP_TRY_BEGIN] = &&L_TRY_BEGIN, [OP_TRY_END] = &&L_TRY_END,
        [OP_STRLEN] = &&L_STRLEN, [OP_STRCAT] = &&L_STRCAT,
        [OP_STRUPPER] = &&L_STRUPPER, [OP_STRLOWER] = &&L_STRLOWER, [OP_STRSUB] = &&L_STRSUB,
        [OP_STRFIND] = &&L_STRFIND, [OP_STRTRIM] = &&L_STRTRIM,
        [OP_STRREPLACE] = &&L_STRREPLACE, [OP_STRFORMAT] = &&L_STRFORMAT,
        [OP_MATH] = &&L_MATH, [OP_CAST] = &&L_CAST,
        [OP_PRINT_I32] = &&L_PRINT_I32, [OP_PRINT_I64] = &&L_PRINT_I64,
        [OP_PRINT_F32] = &&L_PRINT_F32, [OP_PRINT_F64] = &&L_PRINT_F64,
        [OP_PRINT_BOOL] = &&L_PRINT_BOOL, [OP_PRINT_STR] = &&L_PRINT_STR,
        [OP_ARR_NEW] = &&L_ARR_NEW, [OP_ARR_LEN] = &&L_ARR_LEN,
        [OP_ARR_GET] = &&L_ARR_GET, [OP_ARR_SET] = &&L_ARR_SET,
        [OP_ARR_PUSH] = &&L_ARR_PUSH, [OP_ARR_POP] = &&L_ARR_POP,
        [OP_MAP_NEW] = &&L_MAP_NEW, [OP_MAP_LEN] = &&L_MAP_LEN,
        [OP_MAP_GET] = &&L_MAP_GET, [OP_MAP_SET] = &&L_MAP_SET,
        [OP_MAP_HAS] = &&L_MAP_HAS, [OP_MAP_DEL] = &&L_MAP_DEL,
        [OP_MAP_KEY_AT] = &&L_MAP_KEY_AT, [OP_MAP_VAL_AT] = &&L_MAP_VAL_AT,
        [OP_EQ_I32_JF] = &&L_EQ_I32_JF,
        [OP_NE_I32_JF] = &&L_NE_I32_JF,
        [OP_LT_I32_JF] = &&L_LT_I32_JF,
        [OP_LE_I32_JF] = &&L_LE_I32_JF,
        [OP_GT_I32_JF] = &&L_GT_I32_JF,
        [OP_GE_I32_JF] = &&L_GE_I32_JF,
        [OP_EQ_F64_JF] = &&L_EQ_F64_JF,
        [OP_NE_F64_JF] = &&L_NE_F64_JF,
        [OP_LT_F64_JF] = &&L_LT_F64_JF,
        [OP_LE_F64_JF] = &&L_LE_F64_JF,
        [OP_GT_F64_JF] = &&L_GT_F64_JF,
        [OP_GE_F64_JF] = &&L_GE_F64_JF,
        [OP_LI_EQ_I32_JF] = &&L_LI_EQ_I32_JF,
        [OP_LI_NE_I32_JF] = &&L_LI_NE_I32_JF,
        [OP_LI_LT_I32_JF] = &&L_LI_LT_I32_JF,
        [OP_LI_LE_I32_JF] = &&L_LI_LE_I32_JF,
        [OP_LI_GT_I32_JF] = &&L_LI_GT_I32_JF,
        [OP_LI_GE_I32_JF] = &&L_LI_GE_I32_JF,
        [OP_LK_EQ_I32_JF] = &&L_LK_EQ_I32_JF,
        [OP_LK_NE_I32_JF] = &&L_LK_NE_I32_JF,
        [OP_LK_LT_I32_JF] = &&L_LK_LT_I32_JF,
        [OP_LK_LE_I32_JF] = &&L_LK_LE_I32_JF,
        [OP_LK_GT_I32_JF] = &&L_LK_GT_I32_JF,
        [OP_LK_GE_I32_JF] = &&L_LK_GE_I32_JF,
        [OP_LI_SUB_I32] = &&L_LI_SUB_I32,
        [OP_LI_MUL_I32] = &&L_LI_MUL_I32,
        [OP_LI_DIV_I32] = &&L_LI_DIV_I32,
        [OP_LI_MOD_I32] = &&L_LI_MOD_I32,
        [OP_ADD_I32_MV] = &&L_ADD_I32_MV,
        [OP_SUB_I32_MV] = &&L_SUB_I32_MV,
        [OP_MUL_I32_MV] = &&L_MUL_I32_MV,
        [OP_MOD_I32_MV] = &&L_MOD_I32_MV,
        [OP_ADD_IMM_I32_MV] = &&L_ADD_IMM_I32_MV,
        [OP_ADD_I64_MV] = &&L_ADD_I64_MV,
        [OP_ADD_F32_MV] = &&L_ADD_F32_MV,
        [OP_SUB_F32_MV] = &&L_SUB_F32_MV,
        [OP_MUL_F32_MV] = &&L_MUL_F32_MV,
        [OP_ADD_F64_MV] = &&L_ADD_F64_MV,
        [OP_SUB_F64_MV] = &&L_SUB_F64_MV,
        [OP_MUL_F64_MV] = &&L_MUL_F64_MV,
        [OP_STRCAT_MV] = &&L_STRCAT_MV,
    };

    const VMInst * restrict code = chunk->code;
    const VMReg * restrict constants = chunk->constants;
    vm_u32 start_ip = (chunk->entry_func_id >= 0) ? chunk->functions[chunk->entry_func_id].start_ip : 0;
    chunk->active_func_id = chunk->entry_func_id;
    if (chunk->entry_func_id >= 0 && chunk->functions[chunk->entry_func_id].env_map_id >= 0) {
        chunk->active_env_map_id = chunk->functions[chunk->entry_func_id].env_map_id;
    }

    const VMInst * restrict ip = &code[start_ip];
    VMReg * restrict R = &cpu->registers[0];
    VMInst inst;
    vm_u64 max_fuel = cpu->max_instructions;
    vm_u32 func_id = 0;
    vm_i32 next_closure = -1;
    const char *raise_msg = VM_NULL;
    cpu->has_error = VM_FALSE;

    #define VM_DISPATCH() do { inst = *ip++; goto *dispatch_table[VM_GET_OP(inst)]; } while (0)

    /* Fused tail: `jw` is the original JMP_FALSE word, `n` words follow the current one */
    #define VM_FUSED_JF(jw, n) do { VMInst jf_ = (jw); \
        ip += (n) + ((R[VM_GET_A(jf_)].b == VM_FALSE) ? VM_GET_sBx(jf_) : 0); \
        VM_DISPATCH(); } while (0)

    VM_DISPATCH();

    L_EQ_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i32 == R[VM_GET_C(inst)].i32); VM_FUSED_JF(*ip, 1); }
    L_NE_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i32 != R[VM_GET_C(inst)].i32); VM_FUSED_JF(*ip, 1); }
    L_LT_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i32 < R[VM_GET_C(inst)].i32); VM_FUSED_JF(*ip, 1); }
    L_LE_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i32 <= R[VM_GET_C(inst)].i32); VM_FUSED_JF(*ip, 1); }
    L_GT_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i32 > R[VM_GET_C(inst)].i32); VM_FUSED_JF(*ip, 1); }
    L_GE_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i32 >= R[VM_GET_C(inst)].i32); VM_FUSED_JF(*ip, 1); }
    L_EQ_F64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].f64 == R[VM_GET_C(inst)].f64); VM_FUSED_JF(*ip, 1); }
    L_NE_F64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].f64 != R[VM_GET_C(inst)].f64); VM_FUSED_JF(*ip, 1); }
    L_LT_F64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].f64 < R[VM_GET_C(inst)].f64); VM_FUSED_JF(*ip, 1); }
    L_LE_F64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].f64 <= R[VM_GET_C(inst)].f64); VM_FUSED_JF(*ip, 1); }
    L_GT_F64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].f64 > R[VM_GET_C(inst)].f64); VM_FUSED_JF(*ip, 1); }
    L_GE_F64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].f64 >= R[VM_GET_C(inst)].f64); VM_FUSED_JF(*ip, 1); }
    L_LI_EQ_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 == R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LI_NE_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 != R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LI_LT_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 < R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LI_LE_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 <= R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LI_GT_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 > R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LI_GE_I32_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 >= R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LK_EQ_I32_JF: { R[VM_GET_A(inst)] = constants[VM_GET_Bx(inst)]; VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 == R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LK_NE_I32_JF: { R[VM_GET_A(inst)] = constants[VM_GET_Bx(inst)]; VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 != R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LK_LT_I32_JF: { R[VM_GET_A(inst)] = constants[VM_GET_Bx(inst)]; VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 < R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LK_LE_I32_JF: { R[VM_GET_A(inst)] = constants[VM_GET_Bx(inst)]; VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 <= R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LK_GT_I32_JF: { R[VM_GET_A(inst)] = constants[VM_GET_Bx(inst)]; VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 > R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LK_GE_I32_JF: { R[VM_GET_A(inst)] = constants[VM_GET_Bx(inst)]; VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i32 >= R[VM_GET_C(w)].i32); VM_FUSED_JF(ip[1], 2); }
    L_LI_SUB_I32: { R[VM_GET_A(inst)].i32 = (vm_i32)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = R[VM_GET_B(w)].i32 - R[VM_GET_C(w)].i32; ip += 1; VM_DISPATCH(); }
    L_LI_MUL_I32: { R[VM_GET_A(inst)].i32 = (vm_i32)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = R[VM_GET_B(w)].i32 * R[VM_GET_C(w)].i32; ip += 1; VM_DISPATCH(); }
    L_LI_DIV_I32: { R[VM_GET_A(inst)].i32 = (vm_i32)VM_GET_sBx(inst); VMInst w = ip[0]; vm_i32 d = R[VM_GET_C(w)].i32; if (d == 0) goto L_DIV_ZERO; R[VM_GET_A(w)].i32 = vm_div_i32(R[VM_GET_B(w)].i32, d); ip += 1; VM_DISPATCH(); }
    L_LI_MOD_I32: { R[VM_GET_A(inst)].i32 = (vm_i32)VM_GET_sBx(inst); VMInst w = ip[0]; vm_i32 d = R[VM_GET_C(w)].i32; if (d == 0) goto L_DIV_ZERO; R[VM_GET_A(w)].i32 = vm_mod_i32(R[VM_GET_B(w)].i32, d); ip += 1; VM_DISPATCH(); }
    L_ADD_I32_MV: { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 + R[VM_GET_C(inst)].i32; VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_SUB_I32_MV: { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 - R[VM_GET_C(inst)].i32; VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_MUL_I32_MV: { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 * R[VM_GET_C(inst)].i32; VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_MOD_I32_MV: { vm_i32 d = R[VM_GET_C(inst)].i32; if (d == 0) goto L_DIV_ZERO; R[VM_GET_A(inst)].i32 = vm_mod_i32(R[VM_GET_B(inst)].i32, d); VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_ADD_IMM_I32_MV: { R[VM_GET_A(inst)].i32 = R[VM_GET_B(inst)].i32 + (vm_i8)VM_GET_C(inst); VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_ADD_I64_MV: { R[VM_GET_A(inst)].i64 = R[VM_GET_B(inst)].i64 + R[VM_GET_C(inst)].i64; VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_ADD_F32_MV: { R[VM_GET_A(inst)].f32 = R[VM_GET_B(inst)].f32 + R[VM_GET_C(inst)].f32; VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_SUB_F32_MV: { R[VM_GET_A(inst)].f32 = R[VM_GET_B(inst)].f32 - R[VM_GET_C(inst)].f32; VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_MUL_F32_MV: { R[VM_GET_A(inst)].f32 = R[VM_GET_B(inst)].f32 * R[VM_GET_C(inst)].f32; VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_ADD_F64_MV: { R[VM_GET_A(inst)].f64 = R[VM_GET_B(inst)].f64 + R[VM_GET_C(inst)].f64; VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_SUB_F64_MV: { R[VM_GET_A(inst)].f64 = R[VM_GET_B(inst)].f64 - R[VM_GET_C(inst)].f64; VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_MUL_F64_MV: { R[VM_GET_A(inst)].f64 = R[VM_GET_B(inst)].f64 * R[VM_GET_C(inst)].f64; VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_STRCAT_MV: { R[VM_GET_A(inst)].str_id = vm_chunk_concat_strings(chunk, R[VM_GET_B(inst)].str_id, R[VM_GET_C(inst)].str_id); VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }

    L_MOD_I64:      { vm_i64 d = R[VM_GET_C(inst)].i64; if (d == 0) goto L_DIV_ZERO; R[VM_GET_A(inst)].i64 = vm_mod_i64(R[VM_GET_B(inst)].i64, d); VM_DISPATCH(); }
    L_ADD_IMM_I64:  R[VM_GET_A(inst)].i64 = R[VM_GET_B(inst)].i64 + (vm_i8)VM_GET_C(inst); VM_DISPATCH();
    L_LOAD_IMM_I64: R[VM_GET_A(inst)].i64 = (vm_i64)VM_GET_sBx(inst); VM_DISPATCH();
    L_BAND_I64:     R[VM_GET_A(inst)].i64 = R[VM_GET_B(inst)].i64 & R[VM_GET_C(inst)].i64; VM_DISPATCH();
    L_BOR_I64:     R[VM_GET_A(inst)].i64 = R[VM_GET_B(inst)].i64 | R[VM_GET_C(inst)].i64; VM_DISPATCH();
    L_BXOR_I64:     R[VM_GET_A(inst)].i64 = R[VM_GET_B(inst)].i64 ^ R[VM_GET_C(inst)].i64; VM_DISPATCH();
    L_BNOT_I64:     R[VM_GET_A(inst)].i64 = ~R[VM_GET_B(inst)].i64; VM_DISPATCH();
    L_SHL_I64:      R[VM_GET_A(inst)].i64 = (vm_i64)((vm_u64)R[VM_GET_B(inst)].i64 << (R[VM_GET_C(inst)].i64 & 63)); VM_DISPATCH();
    L_SHR_I64:      R[VM_GET_A(inst)].i64 = R[VM_GET_B(inst)].i64 >> (R[VM_GET_C(inst)].i64 & 63); VM_DISPATCH();
    L_I32_TO_I64:   R[VM_GET_A(inst)].i64 = (vm_i64)R[VM_GET_B(inst)].i32; VM_DISPATCH();
    L_I64_TO_I32:   { vm_i32 v = (vm_i32)R[VM_GET_B(inst)].i64; R[VM_GET_A(inst)].raw = 0; R[VM_GET_A(inst)].i32 = v; VM_DISPATCH(); }
    L_JMP_NZ_I64:   if (R[VM_GET_A(inst)].i64 == 0) ip += VM_GET_sBx(inst); VM_DISPATCH();
    L_FORLOOP_I64: {
        vm_u8 a = VM_GET_A(inst);
        vm_i64 next_val = R[a].i64 + 1;
        R[a].i64 = next_val;
        if (__builtin_expect(next_val < R[a + 1].i64, 1)) {
            if (__builtin_expect(chunk->gc_pending, 0)) vm_gc_collect(chunk);   /* safepoint */
            /* `for` loops spend fuel like `while` loops (a sandbox must stop both) */
            if (__builtin_expect(max_fuel > 0, 0) && ++cpu->executed_loops > max_fuel) goto L_FUEL_OUT;
            ip += VM_GET_sBx(inst);
        }
        VM_DISPATCH();
    }

    L_EQ_I64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i64 == R[VM_GET_C(inst)].i64); VM_FUSED_JF(*ip, 1); }
    L_NE_I64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i64 != R[VM_GET_C(inst)].i64); VM_FUSED_JF(*ip, 1); }
    L_LT_I64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i64 < R[VM_GET_C(inst)].i64); VM_FUSED_JF(*ip, 1); }
    L_LE_I64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i64 <= R[VM_GET_C(inst)].i64); VM_FUSED_JF(*ip, 1); }
    L_GT_I64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i64 > R[VM_GET_C(inst)].i64); VM_FUSED_JF(*ip, 1); }
    L_GE_I64_JF: { R[VM_GET_A(inst)].i32 = (vm_i32)(R[VM_GET_B(inst)].i64 >= R[VM_GET_C(inst)].i64); VM_FUSED_JF(*ip, 1); }
    L_LI_EQ_I64_JF: { R[VM_GET_A(inst)].i64 = (vm_i64)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i64 == R[VM_GET_C(w)].i64); VM_FUSED_JF(ip[1], 2); }
    L_LI_NE_I64_JF: { R[VM_GET_A(inst)].i64 = (vm_i64)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i64 != R[VM_GET_C(w)].i64); VM_FUSED_JF(ip[1], 2); }
    L_LI_LT_I64_JF: { R[VM_GET_A(inst)].i64 = (vm_i64)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i64 < R[VM_GET_C(w)].i64); VM_FUSED_JF(ip[1], 2); }
    L_LI_LE_I64_JF: { R[VM_GET_A(inst)].i64 = (vm_i64)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i64 <= R[VM_GET_C(w)].i64); VM_FUSED_JF(ip[1], 2); }
    L_LI_GT_I64_JF: { R[VM_GET_A(inst)].i64 = (vm_i64)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i64 > R[VM_GET_C(w)].i64); VM_FUSED_JF(ip[1], 2); }
    L_LI_GE_I64_JF: { R[VM_GET_A(inst)].i64 = (vm_i64)VM_GET_sBx(inst); VMInst w = ip[0]; R[VM_GET_A(w)].i32 = (vm_i32)(R[VM_GET_B(w)].i64 >= R[VM_GET_C(w)].i64); VM_FUSED_JF(ip[1], 2); }
    L_ADD_IMM_I64_MV: { R[VM_GET_A(inst)].i64 = R[VM_GET_B(inst)].i64 + (vm_i8)VM_GET_C(inst); VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }
    L_SUB_I64_MV: { R[VM_GET_A(inst)].i64 = R[VM_GET_B(inst)].i64 - R[VM_GET_C(inst)].i64; VMInst w = ip[0]; R[VM_GET_A(w)] = R[VM_GET_B(w)]; ip += 1; VM_DISPATCH(); }

    L_LOADK:        R[VM_GET_A(inst)] = constants[VM_GET_Bx(inst)]; VM_DISPATCH();
    L_LOAD_IMM_I32: R[VM_GET_A(inst)].i32 = (vm_i32)VM_GET_sBx(inst); VM_DISPATCH();
    L_LOAD_BOOL:    R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(VM_GET_B(inst)); VM_DISPATCH();
    L_MOVE:         R[VM_GET_A(inst)] = R[VM_GET_B(inst)]; VM_DISPATCH();

    L_ADD_I32:      vm_alu_add_i32(R, inst); VM_DISPATCH();
    L_SUB_I32:      vm_alu_sub_i32(R, inst); VM_DISPATCH();
    L_MUL_I32:      vm_alu_mul_i32(R, inst); VM_DISPATCH();
    L_DIV_I32:      { vm_i32 d = R[VM_GET_C(inst)].i32; if (d == 0) goto L_DIV_ZERO; R[VM_GET_A(inst)].i32 = vm_div_i32(R[VM_GET_B(inst)].i32, d); VM_DISPATCH(); }
    L_MOD_I32:      { vm_i32 d = R[VM_GET_C(inst)].i32; if (d == 0) goto L_DIV_ZERO; R[VM_GET_A(inst)].i32 = vm_mod_i32(R[VM_GET_B(inst)].i32, d); VM_DISPATCH(); }
    L_ADD_IMM_I32:  vm_alu_add_imm_i32(R, inst); VM_DISPATCH();

    L_BAND_I32:     vm_alu_band_i32(R, inst); VM_DISPATCH();
    L_BOR_I32:      vm_alu_bor_i32(R, inst); VM_DISPATCH();
    L_BXOR_I32:     vm_alu_bxor_i32(R, inst); VM_DISPATCH();
    L_BNOT_I32:     vm_alu_bnot_i32(R, inst); VM_DISPATCH();
    L_SHL_I32:      vm_alu_shl_i32(R, inst); VM_DISPATCH();
    L_SHR_I32:      vm_alu_shr_i32(R, inst); VM_DISPATCH();

    L_ADD_I64:      vm_alu_add_i64(R, inst); VM_DISPATCH();
    L_SUB_I64:      vm_alu_sub_i64(R, inst); VM_DISPATCH();
    L_MUL_I64:      vm_alu_mul_i64(R, inst); VM_DISPATCH();
    L_DIV_I64:      { vm_i64 d = R[VM_GET_C(inst)].i64; if (d == 0) goto L_DIV_ZERO; R[VM_GET_A(inst)].i64 = vm_div_i64(R[VM_GET_B(inst)].i64, d); VM_DISPATCH(); }

    L_ADD_F32:      vm_fpu_add_f32(R, inst); VM_DISPATCH();
    L_SUB_F32:      vm_fpu_sub_f32(R, inst); VM_DISPATCH();
    L_MUL_F32:      vm_fpu_mul_f32(R, inst); VM_DISPATCH();
    L_DIV_F32:      vm_fpu_div_f32(R, inst); VM_DISPATCH();

    L_ADD_F64:      vm_fpu_add_f64(R, inst); VM_DISPATCH();
    L_SUB_F64:      vm_fpu_sub_f64(R, inst); VM_DISPATCH();
    L_MUL_F64:      vm_fpu_mul_f64(R, inst); VM_DISPATCH();
    L_DIV_F64:      vm_fpu_div_f64(R, inst); VM_DISPATCH();

    L_EQ_I32:       vm_cmp_eq_i32(R, inst); VM_DISPATCH();
    L_NE_I32:       vm_cmp_ne_i32(R, inst); VM_DISPATCH();
    L_LT_I32:       vm_cmp_lt_i32(R, inst); VM_DISPATCH();
    L_LE_I32:       vm_cmp_le_i32(R, inst); VM_DISPATCH();
    L_GT_I32:       vm_cmp_gt_i32(R, inst); VM_DISPATCH();
    L_GE_I32:       vm_cmp_ge_i32(R, inst); VM_DISPATCH();

    L_EQ_F32:       vm_cmp_eq_f32(R, inst); VM_DISPATCH();
    L_NE_F32:       vm_cmp_ne_f32(R, inst); VM_DISPATCH();
    L_LT_F32:       vm_cmp_lt_f32(R, inst); VM_DISPATCH();
    L_LE_F32:       vm_cmp_le_f32(R, inst); VM_DISPATCH();
    L_GT_F32:       vm_cmp_gt_f32(R, inst); VM_DISPATCH();
    L_GE_F32:       vm_cmp_ge_f32(R, inst); VM_DISPATCH();

    L_EQ_I64:       vm_cmp_eq_i64(R, inst); VM_DISPATCH();
    L_NE_I64:       vm_cmp_ne_i64(R, inst); VM_DISPATCH();
    L_LT_I64:       vm_cmp_lt_i64(R, inst); VM_DISPATCH();
    L_LE_I64:       vm_cmp_le_i64(R, inst); VM_DISPATCH();
    L_GT_I64:       vm_cmp_gt_i64(R, inst); VM_DISPATCH();
    L_GE_I64:       vm_cmp_ge_i64(R, inst); VM_DISPATCH();

    L_EQ_F64:       vm_cmp_eq_f64(R, inst); VM_DISPATCH();
    L_NE_F64:       vm_cmp_ne_f64(R, inst); VM_DISPATCH();
    L_LT_F64:       vm_cmp_lt_f64(R, inst); VM_DISPATCH();
    L_LE_F64:       vm_cmp_le_f64(R, inst); VM_DISPATCH();
    L_GT_F64:       vm_cmp_gt_f64(R, inst); VM_DISPATCH();
    L_GE_F64:       vm_cmp_ge_f64(R, inst); VM_DISPATCH();

    L_EQ_STR:       R[VM_GET_A(inst)].i32 = (vm_i32)vm_chunk_str_eq(chunk, R[VM_GET_B(inst)].str_id, R[VM_GET_C(inst)].str_id); VM_DISPATCH();
    L_NE_STR:       R[VM_GET_A(inst)].i32 = (vm_i32)!vm_chunk_str_eq(chunk, R[VM_GET_B(inst)].str_id, R[VM_GET_C(inst)].str_id); VM_DISPATCH();
    L_LT_STR:       R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(vm_chunk_str_cmp(chunk, R[VM_GET_B(inst)].str_id, R[VM_GET_C(inst)].str_id) <  0); VM_DISPATCH();
    L_LE_STR:       R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(vm_chunk_str_cmp(chunk, R[VM_GET_B(inst)].str_id, R[VM_GET_C(inst)].str_id) <= 0); VM_DISPATCH();
    L_GT_STR:       R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(vm_chunk_str_cmp(chunk, R[VM_GET_B(inst)].str_id, R[VM_GET_C(inst)].str_id) >  0); VM_DISPATCH();
    L_GE_STR:       R[VM_GET_A(inst)].i32 = (vm_i32)vm_bool_from_int(vm_chunk_str_cmp(chunk, R[VM_GET_B(inst)].str_id, R[VM_GET_C(inst)].str_id) >= 0); VM_DISPATCH();

    L_AND_BOOL:     vm_logic_and(R, inst); VM_DISPATCH();
    L_OR_BOOL:      vm_logic_or(R, inst); VM_DISPATCH();
    L_NOT_BOOL:     vm_logic_not(R, inst); VM_DISPATCH();

    L_JMP: {
        vm_i16 off = VM_GET_sBx(inst);
        if (__builtin_expect(off < 0 && chunk->gc_pending, 0)) vm_gc_collect(chunk);   /* safepoint */
        if (__builtin_expect(max_fuel > 0 && off < 0, 0)) {
            if (++cpu->executed_loops > max_fuel) goto L_FUEL_OUT;
        }
        ip += off;
        VM_DISPATCH();
    }
    L_FUEL_OUT:
        if (cpu->catch_top_level == VM_FALSE) vm_sys_write_cstr("[VM Sandbox] Execution fuel limit exceeded\n");
        cpu->has_error = VM_TRUE;
        return R[0];
    L_JMP_FALSE:    if (R[VM_GET_A(inst)].b == VM_FALSE) ip += VM_GET_sBx(inst); VM_DISPATCH();
    L_JMP_TRUE:     if (R[VM_GET_A(inst)].b == VM_TRUE)  ip += VM_GET_sBx(inst); VM_DISPATCH();
    L_JMP_NZ_I32:   if (R[VM_GET_A(inst)].i32 == 0)      ip += VM_GET_sBx(inst); VM_DISPATCH();

    L_FORLOOP_I32: {
        vm_u8 a = VM_GET_A(inst);
        vm_i32 next_val = R[a].i32 + 1;
        R[a].i32 = next_val;
        if (__builtin_expect(next_val < R[a + 1].i32, 1)) {
            if (__builtin_expect(chunk->gc_pending, 0)) vm_gc_collect(chunk);   /* safepoint */
            /* `for` loops spend fuel like `while` loops (a sandbox must stop both) */
            if (__builtin_expect(max_fuel > 0, 0) && ++cpu->executed_loops > max_fuel) goto L_FUEL_OUT;
            ip += VM_GET_sBx(inst);
        }
        VM_DISPATCH();
    }


    L_CALL_REG: {
        vm_i32 fv = R[VM_GET_B(inst)].i32;
        if (VM_IS_CLOSURE(fv)) {
            vm_u32 cid = (vm_u32)(fv & ~VM_CLOSURE_TAG);
            if (__builtin_expect(cid >= chunk->num_closures, 0)) {
                raise_msg = "call of invalid function value";
                goto L_RAISE;
            }
            func_id = chunk->closures[cid].func_id;
            next_closure = (vm_i32)cid;
        } else {
            func_id = (vm_u32)fv;
            next_closure = -1;
            if (__builtin_expect(func_id >= chunk->num_functions, 0)) {
                raise_msg = "call of invalid function value";
                goto L_RAISE;
            }
        }
        goto L_DO_CALL;
    }

    L_CALL:
        func_id = VM_GET_Bx(inst);
        next_closure = cpu->cur_closure;   /* direct calls (incl. self-recursion) keep the closure */
    L_DO_CALL: {
        if (__builtin_expect(chunk->gc_pending, 0)) vm_gc_collect(chunk);   /* safepoint */
        vm_u8 ret_reg = VM_GET_A(inst);
        VMFunctionInfo *callee = &chunk->functions[func_id];
        vm_u8 ret_slots = (callee->return_reg_count > 0) ? callee->return_reg_count : 1;
        if (__builtin_expect(callee->native_fn != VM_NULL, 0)) {
            vm_i32 saved_native = chunk->calling_native_fid;
            chunk->calling_native_fid = (vm_i32)func_id;
            R[ret_reg] = callee->native_fn(chunk, &R[ret_reg + ret_slots], callee->arity);
            chunk->calling_native_fid = saved_native;
            if (__builtin_expect(chunk->native_error, 0)) {
                chunk->native_error = VM_FALSE;
                if (chunk->exit_requested) {   /* os.exit: unwind everything, traps included */
                    cpu->trap_count = 0;
                    cpu->frame_count = 0;
                    return R[0];
                }
                raise_msg = chunk->native_error_msg;
                goto L_RAISE;
            }
            VM_DISPATCH();
        }
        if (__builtin_expect(callee->start_ip == 0xFFFFFFFFu, 0)) {
            raise_msg = "call of a function that was declared but never defined";
            goto L_RAISE;
        }
        vm_u32 cur_base = (vm_u32)(R - cpu->registers);
        /* Callee window starts at ret_reg + ret_slots and may use up to 256 registers */
        if (__builtin_expect(cpu->frame_count >= VM_MAX_CALL_FRAMES ||
                             cur_base + ret_reg + ret_slots + 256u > VM_MAX_REGISTERS, 0)) {
            raise_msg = "stack overflow";
            goto L_RAISE;
        }

        VMCallFrame *frame = &cpu->frames[cpu->frame_count++];
        frame->return_ip = (vm_u32)(ip - code);
        frame->base_reg = cur_base;
        frame->arena_mark = chunk->string_arena_used;
        frame->num_strings_mark = chunk->num_strings;
        frame->prev_env_map_id = chunk->active_env_map_id;
        frame->prev_func_id = chunk->active_func_id;
        frame->ret_reg = ret_reg;
        frame->func_id = (vm_u16)func_id;
        frame->prev_closure = cpu->cur_closure;
        cpu->cur_closure = next_closure;

        chunk->active_func_id = (vm_i32)func_id;
        if (callee->env_map_id >= 0) {
            chunk->active_env_map_id = callee->env_map_id;
        }

        R = &cpu->registers[cur_base + ret_reg + ret_slots];
        ip = &code[callee->start_ip];
        VM_DISPATCH();
    }

    L_RET: {
        vm_u8 src_reg = VM_GET_A(inst);
        vm_u8 count = VM_GET_B(inst);
        if (cpu->frame_count == 0) return R[src_reg];
        VMCallFrame *frame = &cpu->frames[--cpu->frame_count];
        VMReg *caller_R = &cpu->registers[frame->base_reg];
        cpu->cur_closure = frame->prev_closure;
        chunk->active_env_map_id = frame->prev_env_map_id;
        chunk->active_func_id = frame->prev_func_id;

        if (count <= 1) {
            caller_R[frame->ret_reg] = R[src_reg];
        } else {
            for (vm_u8 k = 0; k < count; k++) caller_R[frame->ret_reg + k] = R[src_reg + k];
        }
        R = caller_R;
        ip = &code[frame->return_ip];
        VM_DISPATCH();
    }

    L_GET_OUTER: {
        vm_u8 mode = VM_GET_C(inst);
        if (mode & 0x80u) {
            /* Snapshot outer register R[B] into closure function A's upvalue slot (mode & 7) */
            chunk->functions[VM_GET_A(inst)].upvalues[mode & 0x07u] = R[VM_GET_B(inst)];
            VM_DISPATCH();
        }
        if (mode & 0x40u) {
            /* Mutate outer variable: write R[A] to upvalue slot (mode & 7) and enclosing frame if active */
            vm_u8 slot = mode & 0x07u;
            VMReg val = R[VM_GET_A(inst)];
            if (chunk->active_func_id >= 0) {
                VMFunctionInfo *cur_fn = &chunk->functions[chunk->active_func_id];
                cur_fn->upvalues[slot] = val;
                if (cpu->frame_count > 0 && cur_fn->enclosing_func_id >= 0) {
                    for (vm_u32 fk = cpu->frame_count; fk > 0; fk--) {
                        if ((vm_i32)cpu->frames[fk - 1].func_id == cur_fn->enclosing_func_id) {
                            vm_u32 obase = (fk >= 2) ? cpu->frames[fk - 2].base_reg : 0;
                            if (fk == cpu->frame_count) obase = cpu->frames[fk - 1].base_reg;
                            cpu->registers[obase + VM_GET_B(inst)] = val;
                            break;
                        }
                    }
                }
            }
            VM_DISPATCH();
        }
        if (mode & 0x20u) {
            /* Read outer variable into R[A]: from live parent frame if active, else from upvalues[slot] */
            vm_u8 slot = mode & 0x07u;
            if (chunk->active_func_id >= 0) {
                VMFunctionInfo *cur_fn = &chunk->functions[chunk->active_func_id];
                if (cpu->frame_count > 0 &&
                    cur_fn->enclosing_func_id >= 0 &&
                    (vm_i32)cpu->frames[cpu->frame_count - 1].prev_func_id == cur_fn->enclosing_func_id) {
                    VMReg *outer_R = &cpu->registers[cpu->frames[cpu->frame_count - 1].base_reg];
                    VMReg v = outer_R[VM_GET_B(inst)];
                    cur_fn->upvalues[slot] = v;
                    R[VM_GET_A(inst)] = v;
                } else {
                    R[VM_GET_A(inst)] = cur_fn->upvalues[slot];
                }
            }
            VM_DISPATCH();
        }
        vm_u8 depth = mode;
        if (depth == 0) depth = 1;
        vm_u32 fidx = (cpu->frame_count >= depth) ? (cpu->frame_count - depth) : 0;
        VMReg *outer_R = &cpu->registers[cpu->frames[fidx].base_reg];
        R[VM_GET_A(inst)] = outer_R[VM_GET_B(inst)];
        VM_DISPATCH();
    }

    L_CLOSURE: {
        vm_u32 fid = VM_GET_Bx(inst);
        VMFunctionInfo *cf = &chunk->functions[fid];
        vm_i32 cid = vm_chunk_closure_new(chunk, fid, cf->num_upvals);
        if (__builtin_expect(cid < 0, 0)) {
            raise_msg = "out of memory creating closure";
            goto L_RAISE;
        }
        VMClosure *cl = &chunk->closures[cid];
        vm_u32 *cells = &chunk->closure_cells[cl->first_cell];
        for (vm_u8 k = 0; k < cf->num_upvals; k++) {
            vm_u8 idx = cf->upv_index[k];
            switch (cf->upv_kind[k]) {
                case VM_UPV_CELL:
                    cells[k] = (vm_u32)R[idx].i32;
                    break;
                case VM_UPV_SNAPSHOT:
                    cells[k] = vm_chunk_cell_new(chunk, R[idx], (VMDataType)cf->upv_type[k]);
                    break;
                default: {
                    const VMClosure *parent = &chunk->closures[cpu->cur_closure];
                    cells[k] = chunk->closure_cells[parent->first_cell + idx];
                    break;
                }
            }
        }
        R[VM_GET_A(inst)].i32 = (vm_i32)((vm_u32)cid | VM_CLOSURE_TAG);
        VM_DISPATCH();
    }

    L_CELL_NEW:
        R[VM_GET_A(inst)].i32 = (vm_i32)vm_chunk_cell_new(chunk, R[VM_GET_B(inst)], (VMDataType)VM_GET_C(inst));
        VM_DISPATCH();

    L_CELL_GET:
        R[VM_GET_A(inst)] = chunk->cells[(vm_u32)R[VM_GET_B(inst)].i32];
        VM_DISPATCH();

    L_CELL_SET:
        chunk->cells[(vm_u32)R[VM_GET_A(inst)].i32] = R[VM_GET_B(inst)];
        VM_DISPATCH();

    L_UPVAL_GET: {
        const VMClosure *cl = &chunk->closures[cpu->cur_closure];
        R[VM_GET_A(inst)] = chunk->cells[chunk->closure_cells[cl->first_cell + VM_GET_B(inst)]];
        VM_DISPATCH();
    }

    L_UPVAL_SET: {
        const VMClosure *cl = &chunk->closures[cpu->cur_closure];
        chunk->cells[chunk->closure_cells[cl->first_cell + VM_GET_A(inst)]] = R[VM_GET_B(inst)];
        VM_DISPATCH();
    }

    L_DIV_ZERO:
        trap_kind = 0;
        trap_msg = vm_chunk_add_string(chunk, "division by zero", 16);
        trap_level = 1;
        goto L_TRAP_RAISE;
    L_TRAP:
        trap_kind = VM_GET_A(inst);
        trap_msg = R[VM_GET_B(inst)].str_id;
        trap_level = R[VM_GET_C(inst)].i32;
    L_TRAP_RAISE: {
        vm_u8 kind = trap_kind; /* 0 = error/assert, 1 = warn, 2 = trace, 3 = debug */
        vm_u32 msg_sid = trap_msg;
        vm_i32 level = trap_level;
        vm_u32 report_ip = (vm_u32)((ip - 1) - code);
        if (level > 1 && cpu->frame_count > 0) {
            vm_u32 steps = (vm_u32)(level - 1);
            vm_u32 fidx = (steps <= cpu->frame_count) ? (cpu->frame_count - steps) : 0;
            if (cpu->frames[fidx].return_ip > 0) {
                report_ip = cpu->frames[fidx].return_ip - 1;
            }
        }
        if (kind == 0) {
            chunk->last_error_str_id = msg_sid;
            char tbuf[512];
            vm_u32 tlen = 0;
            if (level > 0) {
                const char *fnm = chunk->file_names[chunk->code_files[report_ip]];
                for (vm_u32 i = 0; fnm[i] && tlen < 480; i++) tbuf[tlen++] = fnm[i];
                tbuf[tlen++] = ':';
                char lbuf[24];
                vm_u32 llen = vm_i64_to_str(lbuf, (vm_i64)chunk->code_lines[report_ip]);
                for (vm_u32 i = 0; i < llen && tlen < 480; i++) tbuf[tlen++] = lbuf[i];
                tbuf[tlen++] = ':';
                tbuf[tlen++] = ' ';
            }
            VMString ms = chunk->strings[msg_sid];
            for (vm_u32 i = 0; i < ms.length && tlen < 480; i++) tbuf[tlen++] = ms.chars[i];
            for (vm_u32 fk = cpu->frame_count; fk > 0 && tlen < 440; fk--) {
                VMCallFrame *cf = &cpu->frames[fk - 1];
                const char *fname = chunk->functions[cf->func_id].name;
                vm_u32 caller_ip = (cf->return_ip > 0) ? (cf->return_ip - 1) : 0;
                const char *cfile = chunk->file_names[chunk->code_files[caller_ip]];
                const char *pfx = "\n  at ";
                for (vm_u32 i = 0; pfx[i]; i++) tbuf[tlen++] = pfx[i];
                for (vm_u32 i = 0; fname[i] && tlen < 480; i++) tbuf[tlen++] = fname[i];
                tbuf[tlen++] = ' ';
                tbuf[tlen++] = '(';
                for (vm_u32 i = 0; cfile[i] && tlen < 480; i++) tbuf[tlen++] = cfile[i];
                tbuf[tlen++] = ':';
                char clb[24];
                vm_u32 cll = vm_i64_to_str(clb, (vm_i64)chunk->code_lines[caller_ip]);
                for (vm_u32 i = 0; i < cll && tlen < 495; i++) tbuf[tlen++] = clb[i];
                tbuf[tlen++] = ')';
            }
            tbuf[tlen] = '\0';
            chunk->last_traceback_str_id = vm_chunk_add_string(chunk, tbuf, tlen);

            if (cpu->trap_count > 0) {
                VMTrapFrame *tf = &cpu->traps[--cpu->trap_count];
                cpu->frame_count = tf->frame_depth;
                R = &cpu->registers[tf->base_reg];
                R[tf->err_reg].str_id = msg_sid;
                ip = &code[tf->catch_ip];
                VM_DISPATCH();
            }
            if (cpu->catch_top_level == VM_TRUE) {
                cpu->has_error = VM_TRUE;
                cpu->last_error_str_id = msg_sid;
                return R[0];
            }
        }
        const char *tag = (kind == 1) ? "[Warning" :
                          (kind == 2) ? "[Trace" :
                          (kind == 3) ? "[Debug" : "[Runtime Error";
        vm_sys_write_cstr(tag);
        if (level > 0) {
            vm_u8 fid_idx = chunk->code_files[report_ip];
            vm_sys_write_cstr(" ");
            vm_sys_write_cstr(chunk->file_names[fid_idx]);
            vm_sys_write_cstr(":");
            char lbuf[24];
            vm_u32 llen = vm_i64_to_str(lbuf, (vm_i64)chunk->code_lines[report_ip]);
            vm_sys_write(lbuf, llen);
        }
        vm_sys_write_cstr("] ");
        vm_sys_print_str(chunk->strings[msg_sid].chars, chunk->strings[msg_sid].length);
#ifdef VM_DEV_MODE
        if (kind == 0) {
            char clbuf[24];
            vm_u32 cllen = vm_i64_to_str(clbuf, (vm_i64)__LINE__);
            vm_sys_write_cstr("  └─ [VM C Source] ");
            vm_sys_write_cstr(__FILE__);
            vm_sys_write_cstr(":");
            vm_sys_write(clbuf, cllen);
            vm_sys_write_cstr(" in ");
            vm_sys_write_cstr(__func__);
            vm_sys_write_cstr("()\n");
        }
#endif
        if (kind == 0) {
            cpu->has_error = VM_TRUE;
            cpu->last_error_str_id = msg_sid;
            return R[0];
        }
        VM_DISPATCH();
    }

    L_TRY_BEGIN: {
        if (cpu->trap_count < VM_MAX_TRAPS) {
            VMTrapFrame *tf = &cpu->traps[cpu->trap_count++];
            tf->catch_ip = (vm_u32)((ip - code) + VM_GET_sBx(inst));
            tf->frame_depth = cpu->frame_count;
            tf->base_reg = (vm_u32)(R - cpu->registers);
            tf->err_reg = VM_GET_A(inst);
        }
        VM_DISPATCH();
    }

    L_TRY_END: {
        if (cpu->trap_count > 0) cpu->trap_count--;
        VM_DISPATCH();
    }

    L_STRLEN:
        R[VM_GET_A(inst)].i32 = (vm_i32)chunk->strings[R[VM_GET_B(inst)].str_id].length;
        VM_DISPATCH();

    L_STRCAT:
        R[VM_GET_A(inst)].str_id = vm_chunk_concat_strings(chunk, R[VM_GET_B(inst)].str_id, R[VM_GET_C(inst)].str_id);
        VM_DISPATCH();

    L_STRUPPER:
        R[VM_GET_A(inst)].str_id = vm_chunk_upper(chunk, R[VM_GET_B(inst)].str_id);
        VM_DISPATCH();

    L_STRLOWER:
        R[VM_GET_A(inst)].str_id = vm_chunk_lower(chunk, R[VM_GET_B(inst)].str_id);
        VM_DISPATCH();

    L_STRSUB:
        R[VM_GET_A(inst)].str_id = vm_chunk_substr(chunk, R[VM_GET_B(inst)].str_id,
                                                   R[VM_GET_C(inst)].i32,
                                                   R[VM_GET_C(inst) + 1].i32);
        VM_DISPATCH();

    L_STRFIND:
        R[VM_GET_A(inst)].i32 = vm_chunk_str_find(chunk, R[VM_GET_B(inst)].str_id, R[VM_GET_C(inst)].str_id);
        VM_DISPATCH();

    L_STRTRIM:
        R[VM_GET_A(inst)].str_id = vm_chunk_str_trim(chunk, R[VM_GET_B(inst)].str_id);
        VM_DISPATCH();

    L_STRREPLACE:
        R[VM_GET_A(inst)].str_id = vm_chunk_str_replace(chunk, R[VM_GET_B(inst)].str_id,
                                                        R[VM_GET_C(inst)].str_id,
                                                        R[VM_GET_C(inst) + 1].str_id);
        VM_DISPATCH();

    L_STRFORMAT:
        R[VM_GET_A(inst)].str_id = vm_chunk_str_format(chunk, R[VM_GET_B(inst)].str_id, R[VM_GET_C(inst)].str_id);
        VM_DISPATCH();

    L_MATH:
        R[VM_GET_A(inst)] = vm_math_eval(R[VM_GET_B(inst)], VM_GET_C(inst));
        VM_DISPATCH();

    L_CAST:
        R[VM_GET_A(inst)] = vm_chunk_cast_value(chunk, R[VM_GET_B(inst)], VM_GET_C(inst));
        VM_DISPATCH();

    L_PRINT_I32:  vm_sys_print_i32(R[VM_GET_A(inst)].i32); VM_DISPATCH();
    L_PRINT_I64:  vm_sys_print_i64(R[VM_GET_A(inst)].i64); VM_DISPATCH();
    L_PRINT_F32:  vm_sys_print_f32(R[VM_GET_A(inst)].f32); VM_DISPATCH();
    L_PRINT_F64:  vm_sys_print_f64(R[VM_GET_A(inst)].f64); VM_DISPATCH();
    L_PRINT_BOOL: vm_sys_print_bool(R[VM_GET_A(inst)].b);  VM_DISPATCH();
    L_PRINT_STR: {
        vm_u32 sid = R[VM_GET_A(inst)].str_id;
        vm_sys_print_str(chunk->strings[sid].chars, chunk->strings[sid].length);
        VM_DISPATCH();
    }

    L_ARR_NEW:
        R[VM_GET_A(inst)].arr_id = vm_chunk_array_new(chunk, (VMDataType)VM_GET_B(inst));
        VM_DISPATCH();

    L_ARR_LEN:
        R[VM_GET_A(inst)].i32 = (vm_i32)chunk->arrays[R[VM_GET_B(inst)].arr_id].length;
        VM_DISPATCH();

    L_ARR_GET: {
        /* Inline fast path; out-of-range ids/indices fall back to the checked helper. */
        vm_u32 id = R[VM_GET_B(inst)].arr_id;
        vm_i32 idx = R[VM_GET_C(inst)].i32;
        if (id < chunk->num_arrays && idx >= 0 && (vm_u32)idx < chunk->arrays[id].length)
            R[VM_GET_A(inst)] = chunk->arrays[id].items[idx];
        else
            R[VM_GET_A(inst)] = vm_chunk_array_get(chunk, id, idx);
        VM_DISPATCH();
    }

    L_ARR_SET:
        vm_chunk_array_set(chunk, R[VM_GET_A(inst)].arr_id, R[VM_GET_B(inst)].i32, R[VM_GET_C(inst)]);
        VM_DISPATCH();

    L_ARR_PUSH: {
        /* Inline fast path when there is spare capacity; growth goes through the helper. */
        vm_u32 id = R[VM_GET_A(inst)].arr_id;
        if (id < chunk->num_arrays && chunk->arrays[id].length < chunk->arrays[id].capacity) {
            VMArray *a = &chunk->arrays[id];
            a->items[a->length++] = R[VM_GET_B(inst)];
        } else {
            vm_chunk_array_push(chunk, id, R[VM_GET_B(inst)]);
        }
        VM_DISPATCH();
    }

    L_ARR_POP:
        R[VM_GET_A(inst)] = vm_chunk_array_pop(chunk, R[VM_GET_B(inst)].arr_id);
        VM_DISPATCH();

    L_MAP_NEW:
        R[VM_GET_A(inst)].map_id = vm_chunk_map_new(chunk, (VMDataType)VM_GET_B(inst), (VMDataType)VM_GET_C(inst));
        VM_DISPATCH();

    L_MAP_LEN:
        R[VM_GET_A(inst)].i32 = (vm_i32)chunk->maps[R[VM_GET_B(inst)].map_id].length;
        VM_DISPATCH();

    L_MAP_GET:
        R[VM_GET_A(inst)] = vm_chunk_map_get(chunk, R[VM_GET_B(inst)].map_id, R[VM_GET_C(inst)].str_id);
        VM_DISPATCH();

    L_MAP_SET:
        vm_chunk_map_set(chunk, R[VM_GET_A(inst)].map_id, R[VM_GET_B(inst)].str_id, R[VM_GET_C(inst)]);
        VM_DISPATCH();

    L_MAP_HAS:
        R[VM_GET_A(inst)].i32 = (vm_i32)vm_chunk_map_has(chunk, R[VM_GET_B(inst)].map_id, R[VM_GET_C(inst)].str_id);
        VM_DISPATCH();

    L_MAP_DEL:
        R[VM_GET_A(inst)].i32 = (vm_i32)vm_chunk_map_del(chunk, R[VM_GET_B(inst)].map_id, R[VM_GET_C(inst)].str_id);
        VM_DISPATCH();

    L_MAP_KEY_AT:
        R[VM_GET_A(inst)].str_id = vm_chunk_map_key_at(chunk, R[VM_GET_B(inst)].map_id, R[VM_GET_C(inst)].i32);
        VM_DISPATCH();

    L_MAP_VAL_AT:
        R[VM_GET_A(inst)] = vm_chunk_map_val_at(chunk, R[VM_GET_B(inst)].map_id, R[VM_GET_C(inst)].i32);
        VM_DISPATCH();

    L_RAISE: {
        /* VM-internal fault (not a script `error()`): catchable by try/pcall like OP_TRAP */
        vm_u32 msg_sid = vm_chunk_new_string(chunk, raise_msg, vm_str_len(raise_msg));
        chunk->last_error_str_id = msg_sid;
        if (cpu->trap_count > 0) {
            VMTrapFrame *tf = &cpu->traps[--cpu->trap_count];
            cpu->frame_count = tf->frame_depth;
            R = &cpu->registers[tf->base_reg];
            R[tf->err_reg].str_id = msg_sid;
            ip = &code[tf->catch_ip];
            VM_DISPATCH();
        }
        cpu->has_error = VM_TRUE;
        cpu->last_error_str_id = msg_sid;
        if (cpu->catch_top_level == VM_TRUE) return R[0];
        vm_u32 report_ip = (vm_u32)((ip - 1) - code);
        vm_sys_write_cstr("[Runtime Error ");
        vm_sys_write_cstr(chunk->file_names[chunk->code_files[report_ip]]);
        vm_sys_write_cstr(":");
        char lbuf[24];
        vm_u32 llen = vm_i64_to_str(lbuf, (vm_i64)chunk->code_lines[report_ip]);
        vm_sys_write(lbuf, llen);
        vm_sys_write_cstr("] ");
        vm_sys_write_cstr(raise_msg);
        vm_sys_write_cstr("\n");
        return R[0];
    }

    L_HALT:
        return R[0];
}
