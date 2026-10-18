#ifndef COMPILER_DECL_H
#define COMPILER_DECL_H

#include "../scope/context.h"

/* `decl_func.c` */
void c_declare_param(Compiler *c, VMFunctionInfo *fn, Token pname, VMDataType ptype, int psid);
void c_compile_func_or_method(Compiler *c, const char *prefix, const char *custom_name, vm_bool is_pub);

/* `decl_struct.c` */
const char *c_parse_operator_name(Compiler *c);
void        c_compile_top_level_operator(Compiler *c);
void        c_compile_const_decl(Compiler *c, const char *prefix, vm_bool is_exported);
void        c_compile_enum_decl(Compiler *c, vm_bool is_exported);
void        c_compile_type_decl(Compiler *c, vm_bool is_exported);
void        c_compile_struct_or_type_decl(Compiler *c, vm_bool is_exported);

/* `decl_generic.c` */
int  c_instantiate_generic(Compiler *c, int gen_id, const VMDataType *types, vm_u8 num_types);

#endif /* COMPILER_DECL_H */
