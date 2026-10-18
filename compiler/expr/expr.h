#ifndef COMPILER_EXPR_H
#define COMPILER_EXPR_H

#include "../scope/context.h"

/* `expr_lambda.c` */
vm_bool    c_is_lambda_start(Compiler *c);
ExprResult c_compile_lambda_expr(Compiler *c);

/* `expr_call.c` */
vm_bool    c_is_postfix_method_call(Compiler *c);
ExprResult c_compile_postfix_method(Compiler *c, ExprResult self_expr);
ExprResult c_compile_ident_or_call(Compiler *c, Token id);

/* `expr_primary.c` */
ExprResult c_compile_primary(Compiler *c, VMDataType expected_type);

/* `expr_ops.c` */
ExprResult c_compile_expr(Compiler *c, VMDataType expected_type);

#endif /* COMPILER_EXPR_H */
