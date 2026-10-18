#ifndef VM_CORE_ISA_H
#define VM_CORE_ISA_H

#include "vm_types.h"

/*
 * ============================================================================
 * VM KERNEL INSTRUCTION SET ARCHITECTURE (`vm/core/vm_isa.h`)
 * 32-bit Fixed-Width Register Instructions:
 *   Format ABC : [Op: 8][A: 8][B: 8][C: 8]
 *   Format ABx : [Op: 8][A: 8][Bx / sBx: 16]
 * ============================================================================
 */

typedef vm_u32 VMInst;

#define VM_MAKE_ABC(op, a, b, c) \
    (((vm_u32)(op) & 0xFFu) | (((vm_u32)(a) & 0xFFu) << 8) | (((vm_u32)(b) & 0xFFu) << 16) | (((vm_u32)(c) & 0xFFu) << 24))

#define VM_MAKE_ABx(op, a, bx) \
    (((vm_u32)(op) & 0xFFu) | (((vm_u32)(a) & 0xFFu) << 8) | (((vm_u32)(vm_u16)(bx)) << 16))

#define VM_GET_OP(inst)  ((vm_u8)((inst) & 0xFFu))
#define VM_GET_A(inst)   ((vm_u8)(((inst) >> 8) & 0xFFu))
#define VM_GET_B(inst)   ((vm_u8)(((inst) >> 16) & 0xFFu))
#define VM_GET_C(inst)   ((vm_u8)(((inst) >> 24) & 0xFFu))
#define VM_GET_Bx(inst)  ((vm_u16)(((inst) >> 16) & 0xFFFFu))
#define VM_GET_sBx(inst) ((vm_i16)VM_GET_Bx(inst))

typedef enum {
    OP_HALT = 0,

    /* Register & Constant Movement */
    OP_LOADK,
    OP_LOAD_IMM_I32,
    OP_LOAD_BOOL,
    OP_MOVE,

    /* i32 Arithmetic */
    OP_ADD_I32,
    OP_SUB_I32,
    OP_MUL_I32,
    OP_DIV_I32,
    OP_MOD_I32,
    OP_ADD_IMM_I32,

    /* i32 Bitwise Operations */
    OP_BAND_I32,
    OP_BOR_I32,
    OP_BXOR_I32,
    OP_BNOT_I32,
    OP_SHL_I32,
    OP_SHR_I32,

    /* i64 Operations */
    OP_ADD_I64,
    OP_SUB_I64,
    OP_MUL_I64,
    OP_DIV_I64,
    OP_MOD_I64,
    OP_ADD_IMM_I64,     /* R[A].i64 = R[B].i64 + (i8)C */
    OP_LOAD_IMM_I64,    /* R[A].i64 = sBx */
    OP_BAND_I64,
    OP_BOR_I64,
    OP_BXOR_I64,
    OP_BNOT_I64,
    OP_SHL_I64,
    OP_SHR_I64,
    OP_I32_TO_I64,      /* R[A].i64 = (i64)R[B].i32 */
    OP_I64_TO_I32,      /* R[A].i32 = (i32)R[B].i64 (upper bits cleared) */

    /* f32 / float Operations */
    OP_ADD_F32,
    OP_SUB_F32,
    OP_MUL_F32,
    OP_DIV_F32,

    /* f64 Operations */
    OP_ADD_F64,
    OP_SUB_F64,
    OP_MUL_F64,
    OP_DIV_F64,

    /* Comparisons & Boolean Logic */
    OP_EQ_I32,
    OP_NE_I32,
    OP_LT_I32,
    OP_LE_I32,
    OP_GT_I32,
    OP_GE_I32,

    OP_EQ_F32,
    OP_NE_F32,
    OP_LT_F32,
    OP_LE_F32,
    OP_GT_F32,
    OP_GE_F32,

    OP_EQ_I64,
    OP_NE_I64,
    OP_LT_I64,
    OP_LE_I64,
    OP_GT_I64,
    OP_GE_I64,

    OP_EQ_F64,
    OP_NE_F64,
    OP_LT_F64,
    OP_LE_F64,
    OP_GT_F64,
    OP_GE_F64,

    OP_EQ_STR,
    OP_NE_STR,
    OP_LT_STR,
    OP_LE_STR,
    OP_GT_STR,
    OP_GE_STR,

    OP_AND_BOOL,
    OP_OR_BOOL,
    OP_NOT_BOOL,

    /* Control Flow & Loop Superinstructions */
    OP_JMP,
    OP_JMP_FALSE,
    OP_JMP_TRUE,
    OP_JMP_NZ_I32,
    OP_JMP_NZ_I64,
    OP_FORLOOP_I32,
    OP_FORLOOP_I64,

    /* Function Call, Closure Capture, Protected Try/Catch & Return */
    OP_CALL,            /* call functions[Bx], results land at R[A] */
    OP_CALL_REG,        /* call functions[R[B].i32] (lambdas / function values) */
    OP_RET,
    OP_GET_OUTER,       /* R[A] = EnclosingFrame(C)[B] */
    OP_CLOSURE,         /* R[A] = new closure of functions[Bx], capturing per its upv_kind/upv_index */
    OP_CELL_NEW,        /* R[A] = new cell holding R[B] of type C (boxes a captured local) */
    OP_CELL_GET,        /* R[A] = cells[R[B]] */
    OP_CELL_SET,        /* cells[R[A]] = R[B] */
    OP_UPVAL_GET,       /* R[A] = cells[current_closure.upvalue[B]] */
    OP_UPVAL_SET,       /* cells[current_closure.upvalue[A]] = R[B] */
    OP_TRAP,            /* A: 0=error/assert, 1=warn; B: msg_str_reg; C: stack_level_reg */
    OP_TRY_BEGIN,       /* Push trap frame: A=err_reg, sBx=catch_offset */
    OP_TRY_END,         /* Pop trap frame */

    /* VM Native String, Math, Cast & Syscall Operations */
    OP_STRLEN,
    OP_STRCAT,          /* R[A].str_id = concat(R[B].str_id, R[C].str_id) */
    OP_STRUPPER,        /* R[A].str_id = upper(R[B].str_id) */
    OP_STRLOWER,        /* R[A].str_id = lower(R[B].str_id) */
    OP_STRSUB,          /* R[A].str_id = substr(R[B].str_id, start=R[C], count=R[C+1]) */
    OP_STRFIND,         /* R[A].i32    = find(R[B].str_id, sub=R[C].str_id) */
    OP_STRTRIM,         /* R[A].str_id = trim(R[B].str_id) */
    OP_STRREPLACE,      /* R[A].str_id = replace(R[B].str_id, old=R[C].str_id, new=R[C+1].str_id) */
    OP_STRFORMAT,       /* R[A].str_id = format(R[B].str_id, arg=R[C].str_id) */
    OP_MATH,            /* R[A]        = math_op(R[B], kind=C) */
    OP_CAST,            /* R[A] = cast(R[B], src_dst_types = C) */
    OP_PRINT_I32,
    OP_PRINT_I64,
    OP_PRINT_F32,
    OP_PRINT_F64,
    OP_PRINT_BOOL,
    OP_PRINT_STR,

    /* VM Native Array & Map Operations */
    OP_ARR_NEW,         /* R[A].arr_id = new_array(elem_type=B) */
    OP_ARR_LEN,         /* R[A].i32    = array_len(R[B].arr_id) */
    OP_ARR_GET,         /* R[A]        = array_get(R[B].arr_id, idx=R[C].i32) */
    OP_ARR_SET,         /* array_set(R[A].arr_id, idx=R[B].i32, val=R[C]) */
    OP_ARR_PUSH,        /* array_push(R[A].arr_id, val=R[B]) */
    OP_ARR_POP,         /* R[A]        = array_pop(R[B].arr_id) */

    OP_MAP_NEW,         /* R[A].map_id = new_map(key_type=B, val_type=C) */
    OP_MAP_LEN,         /* R[A].i32    = map_len(R[B].map_id) */
    OP_MAP_GET,         /* R[A]        = map_get(R[B].map_id, key=R[C].str_id) */
    OP_MAP_SET,         /* map_set(R[A].map_id, key=R[B].str_id, val=R[C]) */
    OP_MAP_HAS,         /* R[A].b      = map_has(R[B].map_id, key=R[C].str_id) */
    OP_MAP_DEL,         /* R[A].b      = map_del(R[B].map_id, key=R[C].str_id) */
    OP_MAP_KEY_AT,      /* R[A].str_id = map_key_at(R[B].map_id, idx=R[C].i32) */
    OP_MAP_VAL_AT,      /* R[A]        = map_val_at(R[B].map_id, idx=R[C].i32) */

    /* Superinstructions (written only by vm/opt/vm_fuse.c, never by the compiler).
     * Each one runs the exact original 2-3 word sequence in one dispatch; the trailing
     * words stay intact, so a jump into the middle still executes correctly. */
    OP_EQ_I32_JF,
    OP_NE_I32_JF,
    OP_LT_I32_JF,
    OP_LE_I32_JF,
    OP_GT_I32_JF,
    OP_GE_I32_JF,
    OP_EQ_F64_JF,
    OP_NE_F64_JF,
    OP_LT_F64_JF,
    OP_LE_F64_JF,
    OP_GT_F64_JF,
    OP_GE_F64_JF,
    OP_LI_EQ_I32_JF,
    OP_LI_NE_I32_JF,
    OP_LI_LT_I32_JF,
    OP_LI_LE_I32_JF,
    OP_LI_GT_I32_JF,
    OP_LI_GE_I32_JF,
    OP_LK_EQ_I32_JF,
    OP_LK_NE_I32_JF,
    OP_LK_LT_I32_JF,
    OP_LK_LE_I32_JF,
    OP_LK_GT_I32_JF,
    OP_LK_GE_I32_JF,
    OP_LI_SUB_I32,
    OP_LI_MUL_I32,
    OP_LI_DIV_I32,
    OP_LI_MOD_I32,
    OP_ADD_I32_MV,
    OP_SUB_I32_MV,
    OP_MUL_I32_MV,
    OP_MOD_I32_MV,
    OP_ADD_IMM_I32_MV,
    OP_ADD_I64_MV,
    OP_ADD_F32_MV,
    OP_SUB_F32_MV,
    OP_MUL_F32_MV,
    OP_ADD_F64_MV,
    OP_SUB_F64_MV,
    OP_MUL_F64_MV,
    OP_STRCAT_MV,

    OP_EQ_I64_JF, OP_NE_I64_JF, OP_LT_I64_JF, OP_LE_I64_JF, OP_GT_I64_JF, OP_GE_I64_JF,
    OP_LI_EQ_I64_JF, OP_LI_NE_I64_JF, OP_LI_LT_I64_JF, OP_LI_LE_I64_JF, OP_LI_GT_I64_JF, OP_LI_GE_I64_JF,
    OP_ADD_IMM_I64_MV,
    OP_SUB_I64_MV,

    OP_COUNT
} VMOpCode;

const char *vm_opcode_name(VMOpCode op);

#endif /* VM_CORE_ISA_H */
