#include "vm_fuse.h"

static int cmp_index_i32(vm_u8 op) {
    return (op >= OP_EQ_I32 && op <= OP_GE_I32) ? (int)(op - OP_EQ_I32) : -1;
}

static int cmp_index_i64(vm_u8 op) {
    return (op >= OP_EQ_I64 && op <= OP_GE_I64) ? (int)(op - OP_EQ_I64) : -1;
}

static int cmp_index_f64(vm_u8 op) {
    return (op >= OP_EQ_F64 && op <= OP_GE_F64) ? (int)(op - OP_EQ_F64) : -1;
}

static int li_arith(vm_u8 op) {
    switch (op) {
        case OP_SUB_I32: return OP_LI_SUB_I32;
        case OP_MUL_I32: return OP_LI_MUL_I32;
        case OP_DIV_I32: return OP_LI_DIV_I32;
        case OP_MOD_I32: return OP_LI_MOD_I32;
        default:         return -1;
    }
}

static int arith_mv(vm_u8 op) {
    switch (op) {
        case OP_ADD_I32:     return OP_ADD_I32_MV;
        case OP_SUB_I32:     return OP_SUB_I32_MV;
        case OP_MUL_I32:     return OP_MUL_I32_MV;
        case OP_MOD_I32:     return OP_MOD_I32_MV;
        case OP_ADD_IMM_I32: return OP_ADD_IMM_I32_MV;
        case OP_ADD_I64:     return OP_ADD_I64_MV;
        case OP_ADD_IMM_I64: return OP_ADD_IMM_I64_MV;
        case OP_SUB_I64:     return OP_SUB_I64_MV;
        case OP_ADD_F32:     return OP_ADD_F32_MV;
        case OP_SUB_F32:     return OP_SUB_F32_MV;
        case OP_MUL_F32:     return OP_MUL_F32_MV;
        case OP_ADD_F64:     return OP_ADD_F64_MV;
        case OP_SUB_F64:     return OP_SUB_F64_MV;
        case OP_MUL_F64:     return OP_MUL_F64_MV;
        case OP_STRCAT:      return OP_STRCAT_MV;
        default:             return -1;
    }
}

static void set_op(VMInst *w, int op) {
    *w = (*w & ~0xFFu) | ((vm_u32)op & 0xFFu);
}

void vm_fuse_superinstructions(VMChunk *chunk, vm_u32 from_ip, vm_u32 to_ip) {
    VMInst *code = chunk->code;
    vm_u32 i = from_ip;
    while (i < to_ip) {
        vm_u8 op  = VM_GET_OP(code[i]);
        vm_u8 op1 = (i + 1 < to_ip) ? VM_GET_OP(code[i + 1]) : OP_HALT;
        vm_u8 op2 = (i + 2 < to_ip) ? VM_GET_OP(code[i + 2]) : OP_HALT;
        int k;

        /* LOAD (imm|const) ; CMP_I32 ; JMP_FALSE  ->  one dispatch */
        if ((op == OP_LOAD_IMM_I32 || op == OP_LOADK) && (k = cmp_index_i32(op1)) >= 0 && op2 == OP_JMP_FALSE) {
            set_op(&code[i], ((op == OP_LOAD_IMM_I32) ? OP_LI_EQ_I32_JF : OP_LK_EQ_I32_JF) + k);
            i += 3;
            continue;
        }
        if ((k = cmp_index_i32(op)) >= 0 && op1 == OP_JMP_FALSE) {
            set_op(&code[i], OP_EQ_I32_JF + k);
            i += 2;
            continue;
        }
        if (op == OP_LOAD_IMM_I64 && (k = cmp_index_i64(op1)) >= 0 && op2 == OP_JMP_FALSE) {
            set_op(&code[i], OP_LI_EQ_I64_JF + k);
            i += 3;
            continue;
        }
        if ((k = cmp_index_i64(op)) >= 0 && op1 == OP_JMP_FALSE) {
            set_op(&code[i], OP_EQ_I64_JF + k);
            i += 2;
            continue;
        }
        if ((k = cmp_index_f64(op)) >= 0 && op1 == OP_JMP_FALSE) {
            set_op(&code[i], OP_EQ_F64_JF + k);
            i += 2;
            continue;
        }
        if (op == OP_LOAD_IMM_I32 && (k = li_arith(op1)) >= 0) {
            set_op(&code[i], k);
            i += 2;
            continue;
        }
        /* ARITH tmp ; MOVE x, tmp  (assignment `x = a op b`) */
        if ((k = arith_mv(op)) >= 0 && op1 == OP_MOVE && VM_GET_B(code[i + 1]) == VM_GET_A(code[i])) {
            set_op(&code[i], k);
            i += 2;
            continue;
        }
        i++;
    }
}
