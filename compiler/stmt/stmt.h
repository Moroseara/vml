#ifndef COMPILER_STMT_H
#define COMPILER_STMT_H

#include "../scope/context.h"

/* `stmt_branch.c` */
void c_skip_block_tokens(Compiler *c);
void c_compile_match_stmt(Compiler *c);
void c_compile_if_stmt(Compiler *c);

/* `stmt_loop.c` */
void c_compile_while_stmt(Compiler *c);
void c_compile_for_stmt(Compiler *c);

/* `stmt_var.c` */
void c_compile_var_decl(Compiler *c, vm_bool is_const);

/* `stmt_assign.c` */
void c_compile_block(Compiler *c);
void c_compile_statement(Compiler *c);

#endif /* COMPILER_STMT_H */
