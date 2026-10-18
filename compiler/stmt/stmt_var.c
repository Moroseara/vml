#include "stmt.h"
#include "../expr/expr.h"

static void allocate_struct_var(Compiler *c, Token name, int struct_id, vm_bool zero_init, ExprResult *init_res) {
    StructDef *sd = &c->structs[struct_id];
    vm_u8 base_reg = c_declare_local(c, name, sd->field_types[0], VM_FALSE, (VMReg){0});
    LocalVar *lv = &c->locals[c->num_locals - 1];
    lv->struct_id = struct_id;
    lv->elem_count = sd->num_fields;
    for (vm_u8 f = 0; f < sd->num_fields; f++) {
        lv->elem_types[f] = sd->field_types[f];
        if (f > 0) c_alloc_reg(c);
        if (zero_init == VM_TRUE) {
            VMReg init_v = (sd->field_has_default[f] == VM_TRUE) ? sd->field_defaults[f] : (VMReg){0};
            if (sd->field_has_default[f] == VM_FALSE && sd->field_types[f] == VM_TYPE_STRING) {
                init_v.str_id = vm_chunk_add_string(c->chunk, "", 0);
            }
            vm_u8 kr = c_emit_load_const(c, init_v, sd->field_types[f]);
            if (kr != (vm_u8)(base_reg + f)) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(base_reg + f), kr, 0));
            }
            c->next_reg = (vm_u8)(base_reg + f + 1);
        } else if (init_res != VM_NULL && (vm_u8)(init_res->reg + f) != (vm_u8)(base_reg + f)) {
            vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(base_reg + f), (vm_u8)(init_res->reg + f), 0));
        }
    }
}

void c_compile_var_decl(Compiler *c, vm_bool is_const) {
    if (c_match(c, TOK_LPAREN) == VM_TRUE) {
        Token names[MAX_TUPLE_ELEMS];
        vm_u8 count = 0;
        do {
            names[count] = c->cur;
            c_consume(c, TOK_IDENT, "Expected variable name in tuple pattern");
            count++;
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
        c_consume(c, TOK_RPAREN, "Expected ')'");
        c_consume(c, TOK_ASSIGN, "Expected '='");

        vm_u8 saved_reg = c->next_reg;
        ExprResult tup = c_compile_expr(c, VM_TYPE_I32);
        c->next_reg = saved_reg;

        for (vm_u8 i = 0; i < count; i++) {
            VMDataType et = (i < tup.elem_count) ? tup.elem_types[i] : tup.type;
            vm_u8 r = c_declare_local(c, names[i], et, is_const, (VMReg){0});
            if (r != (vm_u8)(tup.reg + i)) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, r, (vm_u8)(tup.reg + i), 0));
            }
        }
        c_match(c, TOK_SEMICOLON);
        return;
    }

    Token name = c->cur;
    c_consume(c, TOK_IDENT, "Expected variable name");

    if (c_match(c, TOK_COMMA) == VM_TRUE) {
        Token names[MAX_TUPLE_ELEMS];
        vm_u8 count = 0;
        names[count++] = name;
        do {
            names[count] = c->cur;
            c_consume(c, TOK_IDENT, "Expected variable name in tuple assignment");
            count++;
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
        c_consume(c, TOK_ASSIGN, "Expected '=' after tuple variable names");

        vm_u8 saved_reg = c->next_reg;
        ExprResult tup = c_compile_expr(c, VM_TYPE_I32);
        c->next_reg = saved_reg;

        for (vm_u8 i = 0; i < count; i++) {
            VMDataType et = (i < tup.elem_count) ? tup.elem_types[i] : tup.type;
            vm_u8 r = c_declare_local(c, names[i], et, is_const, (VMReg){0});
            if (r != (vm_u8)(tup.reg + i)) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, r, (vm_u8)(tup.reg + i), 0));
            }
        }
        c_match(c, TOK_SEMICOLON);
        return;
    }

    VMDataType type = VM_TYPE_I32;
    vm_bool has_explicit_type = VM_FALSE;
    int struct_id = -1;
    int var_func_tid = -1;
    VMDataType decl_elem_type = VM_TYPE_VOID;

    if (c_match(c, TOK_COLON) == VM_TRUE) {
        if (c_check(c, TOK_IDENT) == VM_TRUE) {
            int ftid = c_resolve_type_contract(c, c->cur.start, c->cur.length);
            if (ftid >= 0 && c->type_contracts[ftid].is_func_type == VM_TRUE) {
                var_func_tid = ftid;
            }
        }
        type = c_parse_type_ex(c, &struct_id);
        decl_elem_type = c->last_parsed_elem_type;
        has_explicit_type = VM_TRUE;
        if (struct_id >= 0 && c_match(c, TOK_LT) == VM_TRUE) {
            VMDataType gt = c_parse_type(c);
            c_consume(c, TOK_GT, "Expected '>'");
            if (c->structs[struct_id].is_generic == VM_TRUE) {
                for (vm_u8 f = 0; f < c->structs[struct_id].num_fields; f++) {
                    c->structs[struct_id].field_types[f] = gt;
                }
            }
        }
        if (struct_id >= 0 && c_check(c, TOK_ASSIGN) == VM_FALSE) {
            allocate_struct_var(c, name, struct_id, VM_TRUE, VM_NULL);
            c_match(c, TOK_SEMICOLON);
            return;
        }
    }

    VMReg zero = {0};
    if (type == VM_TYPE_STRING) zero.str_id = vm_chunk_add_string(c->chunk, "", 0);

    if (c_match(c, TOK_ASSIGN) == VM_TRUE) {
        vm_u8 saved_reg = c->next_reg;
        c->last_empty_array_ip = 0xFFFFFFFFu;
        ExprResult init = c_compile_expr(c, type);
        if (has_explicit_type == VM_FALSE) {
            type = init.type;
            struct_id = init.struct_id;
        } else if (struct_id >= 0 && init.struct_id >= 0) {
            /* Prevent Object Slicing when an explicit parent struct type is given for a derived struct value */
            (void)c_check_no_object_slicing(c, struct_id, init.struct_id);
        }
        c->next_reg = saved_reg;

        if (struct_id >= 0) {
            allocate_struct_var(c, name, struct_id, VM_FALSE, &init);
            c_match(c, TOK_SEMICOLON);
            return;
        }

        vm_bool var_const = vm_bool_from_int(is_const == VM_TRUE && init.is_const == VM_TRUE);
        vm_u8 var_reg = c_declare_local(c, name, type, var_const, init.const_val);
        LocalVar *lv = &c->locals[c->num_locals - 1];
        lv->lambda_func_id = init.lambda_func_id;
        lv->lambda_gen_id = init.lambda_gen_id;
        lv->func_type_id = var_func_tid;
        lv->elem_count = init.elem_count;
        for (vm_u8 k = 0; k < init.elem_count; k++) {
            lv->elem_types[k] = init.elem_types[k];
            if (k > 0) c_alloc_reg(c);
        }
        if (type == VM_TYPE_ARRAY && decl_elem_type != VM_TYPE_VOID) {
            /* `let xs: string[] = []`: the annotation decides the element type */
            if (lv->elem_count == 0) lv->elem_count = 1;
            lv->elem_types[0] = decl_elem_type;
            vm_u32 ip = c->last_empty_array_ip;
            if (ip != 0xFFFFFFFFu && VM_GET_OP(c->chunk->code[ip]) == OP_ARR_NEW &&
                VM_GET_A(c->chunk->code[ip]) == init.reg) {
                c->chunk->code[ip] = VM_MAKE_ABC(OP_ARR_NEW, init.reg, (vm_u8)decl_elem_type, 0);
            }
        }
        if (init.lambda_gen_id < 0 && init.reg != var_reg) {
            for (vm_u8 k = 0; k < init.elem_count; k++) {
                vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, (vm_u8)(var_reg + k), (vm_u8)(init.reg + k), 0));
            }
        }
    } else {
        vm_u8 var_reg = c_declare_local(c, name, type, is_const, zero);
        vm_u8 kreg = c_emit_load_const(c, zero, type);
        if (kreg != var_reg) vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_MOVE, var_reg, kreg, 0));
        c->next_reg = (vm_u8)(var_reg + 1);
    }
    c_match(c, TOK_SEMICOLON);
}
