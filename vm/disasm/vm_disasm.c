#include "vm_disasm.h"
#include "../syscall/vm_syscall.h"

static void write_u32_padded(vm_u32 val, int width, char pad) {
    char buf[16];
    int i = 15;
    if (val == 0) {
        buf[i--] = '0';
    } else {
        while (val > 0) {
            buf[i--] = (char)('0' + (val % 10u));
            val /= 10u;
        }
    }
    int digits = 15 - i;
    while (digits < width) {
        buf[i--] = pad;
        digits++;
    }
    vm_sys_write(&buf[i + 1], (vm_size)(15 - i));
}

static void write_i16(vm_i16 val) {
    if (val < 0) {
        vm_sys_write("-", 1);
        write_u32_padded((vm_u32)(-(int)val), 1, ' ');
    } else {
        write_u32_padded((vm_u32)val, 1, ' ');
    }
}

void vm_disassemble_chunk(const VMChunk *chunk) {
    vm_sys_write_cstr("=== VM Kernel Bytecode Image ===\n");
    for (vm_u32 i = 0; i < chunk->code_count; i++) {
        for (vm_u32 f = 0; f < chunk->num_functions; f++) {
            if (chunk->functions[f].start_ip == i) {
                vm_sys_write_cstr("\nfunc ");
                vm_sys_write_cstr(chunk->functions[f].name);
                vm_sys_write_cstr("(): ");
                vm_sys_write_cstr(vm_type_name(chunk->functions[f].return_type));
                vm_sys_write_cstr("\n");
            }
        }
        VMInst inst = chunk->code[i];
        VMOpCode op = (VMOpCode)VM_GET_OP(inst);
        vm_sys_write_cstr("  ");
        write_u32_padded(i, 4, '0');
        vm_sys_write_cstr("  ");
        const char *opname = vm_opcode_name(op);
        vm_u32 len = vm_str_len(opname);
        vm_sys_write(opname, len);
        for (vm_u32 s = len; s < 15; s++) vm_sys_write(" ", 1);
        vm_sys_write_cstr("A=");
        write_u32_padded(VM_GET_A(inst), 1, ' ');
        vm_sys_write_cstr("  B=");
        write_u32_padded(VM_GET_B(inst), 1, ' ');
        vm_sys_write_cstr("  C=");
        write_u32_padded(VM_GET_C(inst), 1, ' ');
        vm_sys_write_cstr("  (sBx=");
        write_i16(VM_GET_sBx(inst));
        vm_sys_write_cstr(")\n");
    }
    vm_sys_write_cstr("================================\n\n");
}
