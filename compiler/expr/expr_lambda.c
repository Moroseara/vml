#include "expr.h"
#include "../stmt/stmt.h"
#include "../decl/decl.h"

vm_bool c_is_lambda_start(Compiler *c) {
    if (c_check(c, TOK_FUNC) == VM_TRUE) return VM_TRUE;
    if (c_check(c, TOK_LT) == VM_TRUE) return VM_TRUE;
    if (c_check(c, TOK_LPAREN) == VM_FALSE) return VM_FALSE;
    Lexer copy = c->lex;
    Token t1 = lexer_next(&copy);
    if (t1.type == TOK_RPAREN) {
        Token t2 = lexer_next(&copy);
        return vm_bool_from_int(t2.type == TOK_LBRACE || t2.type == TOK_COLON);
    }
    if (t1.type == TOK_IDENT) {
        Token t2 = lexer_next(&copy);
        return vm_bool_from_int(t2.type == TOK_COLON);
    }
    return VM_FALSE;
}

/* Compiles Lambda `() {}`, `func(x: i32): i32 {}`, Named Nested `func name() {}`, and Generic `<T>(a: T): T {}` */
ExprResult c_compile_lambda_expr(Compiler *c) {
    ExprResult res = {0, VM_TYPE_I32, VM_FALSE, {0}, 1, {VM_TYPE_I32}, -1, -1, -1, VM_FALSE, VM_FALSE};
    static int lambda_counter = 0;
    lambda_counter++;

    c_match(c, TOK_FUNC);

    char lname[64];
    Token orig_name_tok = {TOK_IDENT, "", 0, c->cur.line, 0, 0.0};
    vm_bool has_name = VM_FALSE;
    if (c_check(c, TOK_IDENT) == VM_TRUE || c_check(c, TOK_LEN) == VM_TRUE ||
        (c->cur.type >= TOK_TYPE_I32 && c->cur.type <= TOK_TYPE_VOID)) {
        orig_name_tok = c->cur;
        has_name = VM_TRUE;
        lname[0] = '$'; lname[1] = 'n'; lname[2] = 'e'; lname[3] = 's'; lname[4] = 't'; lname[5] = '_';
        vm_u32 p = 6;
        char numbuf[16];
        vm_u32 nlen = vm_i64_to_str(numbuf, lambda_counter);
        for (vm_u32 i = 0; i < nlen && p < 48; i++) lname[p++] = numbuf[i];
        lname[p++] = '_';
        for (vm_u32 i = 0; i < c->cur.length && p < 63; i++) lname[p++] = c->cur.start[i];
        lname[p] = '\0';
        c_advance(c);
    } else {
        lname[0] = '$'; lname[1] = 'l'; lname[2] = 'a'; lname[3] = 'm';
        vm_u32 p = 4;
        char numbuf[16];
        vm_u32 nlen = vm_i64_to_str(numbuf, lambda_counter);
        for (vm_u32 i = 0; i < nlen && p < 63; i++) lname[p++] = numbuf[i];
        lname[p] = '\0';
    }

    if (c_match(c, TOK_LT) == VM_TRUE) {
        int gid = c->num_generics++; c->generics[c->num_generics - 1].module_idx = c->cur_module_idx;
        GenericTemplate *gt = &c->generics[gid];
        vm_str_copy(gt->name, lname, vm_str_len(lname), 64);
        gt->num_type_params = 0;
        gt->is_variadic = VM_FALSE;
        do {
            Token tp = c->cur;
            c_consume(c, TOK_IDENT, "Expected generic parameter name in lambda");
            vm_str_copy(gt->type_param_names[gt->num_type_params++], tp.start, tp.length, 32);
            if (c_match(c, TOK_ELLIPSIS) == VM_TRUE) gt->is_variadic = VM_TRUE;
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
        c_consume(c, TOK_GT, "Expected '>' in generic lambda");
        gt->body_src_start = c->cur.start;
        while (c_check(c, TOK_LBRACE) == VM_FALSE && c_check(c, TOK_EOF) == VM_FALSE) c_advance(c);
        c_consume(c, TOK_LBRACE, "Expected '{'");
        int depth = 1;
        while (depth > 0 && c_check(c, TOK_EOF) == VM_FALSE) {
            if (c_match(c, TOK_LBRACE) == VM_TRUE) depth++;
            else if (c_match(c, TOK_RBRACE) == VM_TRUE) depth--;
            else c_advance(c);
        }
        res.lambda_gen_id = gid;
        return res;
    }

    vm_u32 skip_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, 0));

    LocalVar saved_locals[256];
    int saved_num_locals = c->num_locals;
    for (int i = 0; i < saved_num_locals; i++) saved_locals[i] = c->locals[i];

    LocalVar saved_outer[128];
    int saved_num_outer = c->num_outer_locals;
    for (int i = 0; i < saved_num_outer; i++) saved_outer[i] = c->outer_locals[i];

    /* Expose enclosing function's locals as `outer_locals` for closure capture! */
    c->num_outer_locals = (saved_num_locals <= 128) ? saved_num_locals : 128;
    for (int i = 0; i < c->num_outer_locals; i++) c->outer_locals[i] = saved_locals[i];

    int saved_scope = c->scope_depth;
    vm_u8 saved_next = c->next_reg, saved_max = c->max_reg;
    VMDataType saved_ret = c->current_ret_type;
    int saved_ret_sid = c->current_ret_struct_id;

    vm_u32 fid = c_alloc_func_slot(c);
    VMFunctionInfo *fn = &c->chunk->functions[fid];
    vm_str_copy(fn->name, lname, vm_str_len(lname), 64);
    fn->arity = 0;
    fn->min_arity = 0;
    fn->is_pub = VM_TRUE;
    fn->is_deprecated = VM_FALSE;
    fn->deprecated_msg[0] = '\0';
    fn->owner_struct_id = -1;
    fn->return_type = VM_TYPE_VOID;
    fn->return_struct_id = -1;
    fn->return_reg_count = 1;

    c->num_locals = 0; c->scope_depth = 1; c->next_reg = 0; c->max_reg = 0;

    c_consume(c, TOK_LPAREN, "Expected '(' for function/lambda");
    if (c_check(c, TOK_RPAREN) == VM_FALSE) {
        do {
            Token pname = c->cur;
            c_consume(c, TOK_IDENT, "Expected parameter name");
            c_consume(c, TOK_COLON, "Expected ':'");
            int psid = -1;
            VMDataType ptype = c_parse_type_ex(c, &psid);
            vm_u8 pidx = fn->arity;
            c_declare_param(c, fn, pname, ptype, psid);
            if (c_match(c, TOK_ASSIGN) == VM_TRUE) {
                vm_u32 scnt = c->chunk->code_count;
                vm_u8 sr = c->next_reg;
                ExprResult dv = c_compile_expr(c, ptype);
                c->chunk->code_count = scnt;
                c->next_reg = sr;
                fn->param_has_default[pidx] = VM_TRUE;
                fn->param_defaults[pidx] = dv.const_val;
            } else {
                fn->min_arity++;
            }
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
    }
    c_consume(c, TOK_RPAREN, "Expected ')' after parameters");

    if (c_match(c, TOK_COLON) == VM_TRUE) {
        int rsid = -1;
        fn->return_type = c_parse_type_ex(c, &rsid);
        fn->return_struct_id = (vm_i16)rsid;
        if (rsid >= 0) fn->return_reg_count = c->structs[rsid].num_fields;
    }
    c->current_ret_type = fn->return_type;
    c->current_ret_struct_id = fn->return_struct_id;
    fn->start_ip = c->chunk->code_count;

    if (has_name == VM_TRUE) {
        c_declare_local(c, orig_name_tok, VM_TYPE_I32, VM_TRUE, (VMReg){0});
        c->locals[c->num_locals - 1].lambda_func_id = (int)fid;
    }

    c_compile_block(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, 0, fn->return_reg_count, 0));
    fn->num_regs = c->max_reg;

    c->num_locals = saved_num_locals;
    for (int i = 0; i < saved_num_locals; i++) c->locals[i] = saved_locals[i];
    c->num_outer_locals = saved_num_outer;
    for (int i = 0; i < saved_num_outer; i++) c->outer_locals[i] = saved_outer[i];
    c->scope_depth = saved_scope;
    c->next_reg = saved_next; c->max_reg = saved_max;
    c->current_ret_type = saved_ret;
    c->current_ret_struct_id = saved_ret_sid;

    vm_i16 skip_off = (vm_i16)(c->chunk->code_count - (skip_jmp + 1));
    c->chunk->code[skip_jmp] = VM_MAKE_ABx(OP_JMP, 0, skip_off);

    res.reg = c_alloc_reg(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_LOAD_IMM_I32, res.reg, (vm_i16)fid));
    res.lambda_func_id = (int)fid;
    return res;
}
