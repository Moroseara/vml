#include "context.h"
#include "../stmt/stmt.h"

void c_error_at(Compiler *c, const char *msg, const char *c_file, int c_line, const char *c_func) {
    if (c->had_error == VM_TRUE) return;
    c->had_error = VM_TRUE;
    vm_sys_write_cstr("[Compile Error ");
    if (c->chunk != VM_NULL) {
        vm_sys_write_cstr(c->chunk->file_names[c->chunk->current_file_id]);
        vm_sys_write_cstr(":");
    }
    char lbuf[24];
    vm_u32 llen = vm_i64_to_str(lbuf, (vm_i64)c->cur.line);
    vm_sys_write(lbuf, llen);
    vm_sys_write_cstr("] ");
    vm_sys_write_cstr(msg);
    vm_sys_write_cstr(" (near '");
    vm_sys_write(c->cur.start, c->cur.length);
    vm_sys_write_cstr("')\n");
    if (c_file != VM_NULL) {
        vm_sys_write_cstr("  └─ [Compiler C Source] ");
        vm_sys_write_cstr(c_file);
        vm_sys_write_cstr(":");
        vm_u32 clen = vm_i64_to_str(lbuf, (vm_i64)c_line);
        vm_sys_write(lbuf, clen);
        if (c_func != VM_NULL) {
            vm_sys_write_cstr(" in ");
            vm_sys_write_cstr(c_func);
            vm_sys_write_cstr("()");
        }
        vm_sys_write_cstr("\n");
    }
}

void c_advance(Compiler *c) {
    c->cur = lexer_next(&c->lex);
    if (c->chunk != VM_NULL && c->cur.line > 0) {
        c->chunk->current_line = (vm_u16)c->cur.line;
    }
}

vm_bool c_check(Compiler *c, TokenType t) {
    return vm_bool_from_int(c->cur.type == t);
}

vm_bool c_match(Compiler *c, TokenType t) {
    if (c->cur.type == t) {
        c_advance(c);
        return VM_TRUE;
    }
    return VM_FALSE;
}

void c_consume_at(Compiler *c, TokenType t, const char *err, const char *c_file, int c_line, const char *c_func) {
    if (c->cur.type == t) c_advance(c);
    else c_error_at(c, err, c_file, c_line, c_func);
}

vm_u8 c_alloc_reg(Compiler *c) {
    vm_u8 r = c->next_reg++;
    if (c->next_reg > c->max_reg) c->max_reg = c->next_reg;
    return r;
}

void c_begin_scope(Compiler *c) {
    c->scope_depth++;
}

void c_emit_defer_entry(Compiler *c, int idx) {
    Lexer saved_lex = c->lex;
    Token saved_cur = c->cur;
    int saved_num_defers = c->num_defers;
    c->num_defers = idx;
    lexer_init(&c->lex, c->defer_src[idx]);
    c->lex.line = c->defer_line[idx];
    c_advance(c);
    if (c_check(c, TOK_LBRACE) == VM_TRUE) {
        c_compile_block(c);
    } else {
        c_compile_statement(c);
    }
    c->num_defers = saved_num_defers;
    c->lex = saved_lex;
    c->cur = saved_cur;
}

void c_emit_defers_for_return(Compiler *c) {
    for (int i = c->num_defers - 1; i >= 0; i--) {
        c_emit_defer_entry(c, i);
    }
}

void c_end_scope(Compiler *c) {
    while (c->num_defers > 0 && c->defer_scope_depth[c->num_defers - 1] == c->scope_depth) {
        int idx = --c->num_defers;
        c_emit_defer_entry(c, idx);
    }
    while (c->num_locals > 0 && c->locals[c->num_locals - 1].depth == c->scope_depth) {
        c->num_locals--;
    }
    c->scope_depth--;
}

int c_resolve_local(Compiler *c, const char *name, vm_u32 len) {
    for (int i = c->num_locals - 1; i >= 0; i--) {
        if (vm_str_eq(c->locals[i].name, c->locals[i].length, name, len) == VM_TRUE) {
            return i;
        }
    }
    return -1;
}

int c_resolve_outer_local(Compiler *c, const char *name, vm_u32 len) {
    for (int i = c->num_outer_locals - 1; i >= 0; i--) {
        if (vm_str_eq(c->outer_locals[i].name, c->outer_locals[i].length, name, len) == VM_TRUE) {
            return i;
        }
    }
    return -1;
}

int c_resolve_global_const(Compiler *c, const char *name, vm_u32 len) {
    for (int i = c->num_global_consts - 1; i >= 0; i--) {
        if (vm_str_eq(c->global_consts[i].name, c->global_consts[i].length, name, len) == VM_TRUE) {
            return i;
        }
    }
    return -1;
}

vm_u8 c_declare_local(Compiler *c, Token name, VMDataType type, vm_bool is_const, VMReg const_val) {
    vm_u8 reg = c_alloc_reg(c);
    LocalVar *lv = &c->locals[c->num_locals++];
    lv->name = name.start;
    lv->length = name.length;
    lv->type = type;
    lv->reg = reg;
    lv->depth = c->scope_depth;
    lv->is_const = is_const;
    lv->const_val = const_val;
    lv->elem_count = 1;
    lv->elem_types[0] = type;
    lv->struct_id = -1;
    lv->lambda_func_id = -1;
    lv->lambda_gen_id = -1;
    lv->func_type_id = -1;
    lv->boxed = VM_FALSE;
    lv->is_closure = VM_FALSE;
    return reg;
}

vm_u8 c_emit_load_const(Compiler *c, VMReg val, VMDataType type) {
    vm_u8 r = c_alloc_reg(c);
    if (type == VM_TYPE_I32 && val.i32 >= -32768 && val.i32 <= 32767) {
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, r, (vm_i16)val.i32));
    } else if (type == VM_TYPE_I64 && val.i64 >= -32768 && val.i64 <= 32767) {
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I64, r, (vm_i16)val.i64));
    } else if (type == VM_TYPE_BOOL) {
        vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_LOAD_BOOL, r, val.b, 0));
    } else {
        vm_u32 k = vm_chunk_add_constant(c->chunk, val, type);
        vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOADK, r, (vm_u16)k));
    }
    return r;
}
