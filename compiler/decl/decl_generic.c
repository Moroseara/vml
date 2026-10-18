#include "decl.h"
#include "../stmt/stmt.h"

int c_instantiate_generic(Compiler *c, int gen_id, const VMDataType *types, vm_u8 num_types) {
    GenericTemplate *gt = &c->generics[gen_id];
    vm_u32 skip_jmp = vm_chunk_emit(c->chunk, VM_MAKE_ABx(OP_JMP, 0, 0));

    Lexer saved_lex = c->lex; Token saved_cur = c->cur;
    LocalVar saved_locals[256]; int saved_num_locals = c->num_locals;
    for (int i = 0; i < saved_num_locals; i++) saved_locals[i] = c->locals[i];
    int saved_scope = c->scope_depth;
    vm_u8 saved_next = c->next_reg, saved_max = c->max_reg;
    VMDataType saved_ret = c->current_ret_type;

    c->num_active_tparams = num_types;
    for (vm_u8 i = 0; i < 8; i++) c->active_tparam_sids[i] = -1;
    for (vm_u8 i = 0; i < num_types; i++) {
        if (i < gt->num_type_params) {
            vm_str_copy(c->active_tparam_names[i], gt->type_param_names[i], 31, 32);
        }
        c->active_tparam_types[i] = types[i];
    }

    vm_u32 fid = c_alloc_func_slot(c);
    VMFunctionInfo *fn = &c->chunk->functions[fid];
    vm_str_copy(fn->name, gt->name, vm_str_len(gt->name), 64);
    fn->arity = 0; fn->min_arity = 0; fn->is_pub = VM_TRUE; fn->owner_struct_id = -1;
    fn->return_type = VM_TYPE_VOID;
    fn->return_struct_id = -1; fn->return_reg_count = 1;
    c->num_locals = 0; c->scope_depth = 1; c->next_reg = 0; c->max_reg = 0;

    lexer_init(&c->lex, gt->body_src_start);
    c_advance(c);
    c_consume(c, TOK_LPAREN, "Expected '('");
    if (c_check(c, TOK_RPAREN) == VM_FALSE) {
        do {
            if (c_match(c, TOK_ELLIPSIS) == VM_TRUE) {
                c_consume(c, TOK_COLON, "Expected ':'");
                (void)c_parse_type(c);
                Token pack_tok = {TOK_ELLIPSIS, "...", 3, c->cur.line, 0, 0.0};
                c_declare_local(c, pack_tok, types[0], VM_FALSE, (VMReg){0});
                LocalVar *lv = &c->locals[c->num_locals - 1];
                lv->elem_count = num_types;
                for (vm_u8 k = 0; k < num_types; k++) {
                    lv->elem_types[k] = types[k];
                    fn->param_struct_ids[fn->arity] = -1;
                    fn->param_types[fn->arity++] = types[k];
                    fn->min_arity++;
                    if (k > 0) c_alloc_reg(c);
                }
            } else {
                Token pname = c->cur;
                c_consume(c, TOK_IDENT, "Expected param name");
                c_consume(c, TOK_COLON, "Expected ':'");
                VMDataType ptype = c_parse_type(c);
                c_declare_param(c, fn, pname, ptype, -1);
                fn->min_arity++;
            }
        } while (c_match(c, TOK_COMMA) == VM_TRUE);
    }
    c_consume(c, TOK_RPAREN, "Expected ')'");
    if (c_match(c, TOK_COLON) == VM_TRUE) fn->return_type = c_parse_type(c);
    else fn->return_type = types[0];
    c->current_ret_type = fn->return_type;
    fn->start_ip = c->chunk->code_count;

    c_compile_block(c);
    vm_chunk_emit(c->chunk, VM_MAKE_ABC(OP_RET, 0, 1, 0));
    fn->num_regs = c->max_reg;

    c->num_active_tparams = 0; c->lex = saved_lex; c->cur = saved_cur;
    c->num_locals = saved_num_locals;
    for (int i = 0; i < saved_num_locals; i++) c->locals[i] = saved_locals[i];
    c->scope_depth = saved_scope; c->next_reg = saved_next; c->max_reg = saved_max;
    c->current_ret_type = saved_ret;

    c->chunk->code[skip_jmp] = VM_MAKE_ABx(OP_JMP, 0, (vm_i16)(c->chunk->code_count - (skip_jmp + 1)));
    return (int)fid;
}
