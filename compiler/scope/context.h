#ifndef COMPILER_SCOPE_CONTEXT_H
#define COMPILER_SCOPE_CONTEXT_H

#include "../../vm/core/vm_types.h"
#include "../../vm/memory/vm_memory.h"
#include "../../vm/syscall/vm_syscall.h"
#include "../../vm/string/vm_string.h"
#include "../../vm/embed/vm_embed.h"
#include "../lexer/lexer.h"

#define C_PATH_CAP 256   /* longest file path the compiler keeps */

#define MAX_TUPLE_ELEMS   8
#define MAX_STRUCT_FIELDS 16

typedef struct {
    const char *name;
    vm_u32      length;
    VMDataType  type;
    vm_u8       reg;
    int         depth;
    vm_bool     is_const;
    VMReg       const_val;

    vm_u8       elem_count;
    VMDataType  elem_types[MAX_TUPLE_ELEMS];
    int         struct_id;
    int         lambda_func_id;
    int         lambda_gen_id;
    int         func_type_id;
    vm_bool     boxed;        /* captured by an inner function: the register holds a cell id */
    vm_bool     is_closure;   /* holds a closure value (call through OP_CALL_REG) */
} LocalVar;

typedef struct {
    char       name[64];
    vm_u32     length;
    VMDataType type;
    VMReg      val;
} GlobalConst;

#define C_FIDX_BUCKETS 4096
#define C_AIDX_BUCKETS 1024

typedef struct {
    char   alias_name[64];
    vm_i32 func_id;
} FuncAlias;

typedef struct {
    char       name[64];
    int        owner_type_id;
    int        parent_struct_id;
    int        parent_struct_ids[4];
    vm_u8      num_parents;
    int        implemented_types[4];
    vm_u8      num_implemented_types;
    vm_u8      num_fields;
    char       field_names[MAX_STRUCT_FIELDS][32];
    VMDataType field_types[MAX_STRUCT_FIELDS];
    vm_bool    field_is_pub[MAX_STRUCT_FIELDS];
    vm_bool    field_has_default[MAX_STRUCT_FIELDS];
    VMReg      field_defaults[MAX_STRUCT_FIELDS];
    vm_bool    is_generic;
    vm_u8      num_type_params;
    char       type_param_name[32];

    /* Synthetic structs made by the AST front end */
    vm_bool    is_optional;     /* `T?`: fields {has: bool, value: T} */
    VMDataType opt_inner;
    vm_bool    is_enum;         /* enum value: fields {tag: i32, payload slots...} */
    int        enum_first;      /* index of its first variant in Compiler.enum_variants */
    int        enum_count;
    vm_bool    is_result;       /* `T ! E`: fields {ok: bool, value slots (T), error slots (E)} */
    VMDataType res_val_type;    /* T (VOID when the function returns no value) */
    int        res_val_sid;     /* struct id of T, or -1 */
    vm_u8      res_val_w;       /* registers used by T (0 for void) */
    VMDataType res_val_elem;    /* element type when T is an array (VOID = unknown) */
    VMDataType res_err_type;
    int        res_err_sid;
    vm_u8      res_err_w;
} StructDef;

typedef struct {
    char        name[64];
    vm_bool     is_alias;
    vm_bool     is_func_type;
    VMDataType  alias_type;
    int         alias_struct_id;
    int         layout_struct_id;
    int         last_impl_struct_id;
    int         forward_func_id;

    vm_u8       func_arity;
    VMDataType  func_param_types[8];
    int         func_param_sids[8];
    VMDataType  func_ret_type;
    int         func_ret_sid;

    vm_u8       num_req_fields;
    char        req_field_names[16][32];
    VMDataType  req_field_types[16];
    vm_bool     req_field_has_default[16];
    VMReg       req_field_defaults[16];

    vm_u8       num_req_methods;
    char        req_method_names[16][32];
    vm_u8       req_method_arity[16];
    VMDataType  req_method_param_types[16][8];
    int         req_method_param_sids[16][8];
    vm_bool     req_method_param_is_self[16][8];
    VMDataType  req_method_ret[16];
    int         req_method_ret_sid[16];
    vm_bool     req_method_ret_is_self[16];
    const char *req_method_body_src[16];
} TypeContract;

typedef struct {
    char        name[64];
    char        type_param_names[8][32];
    vm_u8       num_type_params;
    vm_bool     is_variadic;
    const char *body_src_start;
    int         module_idx;     /* module that declared it (instances keep its privacy) */
} GenericTemplate;

#define MAX_VARIANT_PAYLOAD 4

typedef struct {
    char       qname[64];
    vm_i32     tag;
    vm_bool    has_payload;
    vm_bool    is_tagged_enum;
    VMDataType payload_type;
    /* AST front end: enum values are structs {tag, payload...} */
    int        enum_sid;
    vm_u8      payload_count;
    VMDataType payload_types[MAX_VARIANT_PAYLOAD];
} EnumVariant;

typedef struct {
    vm_u8      reg;
    VMDataType type;
    vm_bool    is_const;
    VMReg      const_val;

    vm_u8      elem_count;
    VMDataType elem_types[MAX_TUPLE_ELEMS];
    int        struct_id;
    int        lambda_func_id;
    int        lambda_gen_id;
    vm_bool    is_closure;    /* value is a closure: call it with OP_CALL_REG, not OP_CALL */
    vm_bool    is_none;       /* the `none` literal (becomes an empty optional) */
} ExprResult;

#define EXPR_SET_FUNC_TYPE(r, ftid) do { if ((ftid) >= 0) (r).lambda_gen_id = -100 - (ftid); } while (0)
#define EXPR_GET_FUNC_TYPE(r)       (((r).lambda_gen_id <= -100) ? (-100 - (r).lambda_gen_id) : -1)

typedef struct {
    Lexer            lex;
    Token            cur;
    VMChunk         *chunk;
    const VMConfig  *config; /* Feature flags & capability permissions */

    LocalVar         locals[256];
    int              num_locals;
    LocalVar         outer_locals[128];
    int              num_outer_locals;
    int              scope_depth;
    vm_u8            next_reg;
    vm_u8            max_reg;

    GlobalConst      global_consts[256];
    int              num_global_consts;

    FuncAlias        func_aliases[512];
    int              num_func_aliases;

    /* Name -> fid hash index over chunk->functions (chains ascending by fid) and
       func_aliases (chains newest first). See scope_func.c. */
    vm_i32           fidx_head[C_FIDX_BUCKETS];
    vm_i32           fidx_next[VM_MAX_FUNCTIONS];
    vm_u32           fidx_hash[VM_MAX_FUNCTIONS];
    vm_u32           fidx_upto;      /* fids [0, fidx_upto) are indexed */
    vm_i32           aidx_head[C_AIDX_BUCKETS];
    vm_i32           aidx_next[512];

    /* Loaded modules: handle (the name code uses, e.g. `util`) and full name (`my.util`).
       Module internals are named `<handle>.<name>`, so handles are unique per program. */
    char             loaded_modules[32][64];
    char             loaded_full[32][128];
    int              num_loaded_modules;
    int              cur_module_idx;            /* module being compiled; -1 = the main program */
    char             cur_file[C_PATH_CAP];      /* file being compiled (relative imports start here) */
    char             pkg_root[C_PATH_CAP];      /* relative imports may not leave this folder */
    char             path_imported[64][C_PATH_CAP];   /* files already loaded by `import "./x"` */
    int              num_path_imported;
    vm_i8            func_module[VM_MAX_FUNCTIONS];   /* origin module of each function (-1 = program) */
    /* Which modules each module may name (bit = module index; slot 0 = the main program, slot i+1 = module i):
       its own imports plus whatever those modules share with `pub import`. Prelude modules are visible everywhere. */
    vm_u32           mod_visible[33];
    vm_u32           mod_pubexp[33];
    vm_u32           prelude_mask;

    StructDef        structs[64];
    int              num_structs;
    int              current_struct_id;

    TypeContract     type_contracts[32];
    int              num_type_contracts;

    EnumVariant      enum_variants[128];
    int              num_enum_variants;

    GenericTemplate  generics[64];
    int              num_generics;

    char             active_tparam_names[8][32];
    VMDataType       active_tparam_types[8];
    int              active_tparam_sids[8];     /* struct id when the type argument is a struct, else -1 */
    vm_u8            num_active_tparams;

    char             module_prefix[32];

    vm_u32           break_patches[64];
    int              num_breaks;
    vm_u32           continue_patches[64];
    int              num_continues;

    const char      *defer_src[32];
    int              defer_line[32];
    int              defer_scope_depth[32];
    int              num_defers;

    vm_bool          pending_deprecated;
    char             pending_deprecated_msg[64];
    char             pending_host_export[64];
    char             pending_intrinsic[32];   /* `@[intrinsic("name")]` on the next function */
    vm_bool          in_std_module;           /* compiling an embedded stdlib module (may use intrinsics) */

    VMDataType       current_ret_type;
    int              current_ret_struct_id;
    int              current_func_id;
    vm_bool          had_error;

    /* When non-NULL, imported `.vml` module sources are compiled by the AST front end */
    void            *ast_gen;
    void           (*ast_compile_module)(void *gen, const char *src, const char *path);

    /* Element type of the most recent `T[]` annotation (VOID when not an array or unknown) */
    VMDataType       last_parsed_elem_type;
    /* Code index of the most recent empty `[]` literal's OP_ARR_NEW, so a typed declaration can patch it */
    vm_u32           last_empty_array_ip;
} Compiler;

/* Variable & Scope Management (`scope_var.c`) */
void       c_error_at(Compiler *c, const char *msg, const char *c_file, int c_line, const char *c_func);
void       c_consume_at(Compiler *c, TokenType t, const char *err, const char *c_file, int c_line, const char *c_func);
#ifdef VM_DEV_MODE
#define c_error(c, msg)        c_error_at((c), (msg), __FILE__, __LINE__, __func__)
#define c_consume(c, t, err)   c_consume_at((c), (t), (err), __FILE__, __LINE__, __func__)
#else
#define c_error(c, msg)        c_error_at((c), (msg), VM_NULL, 0, VM_NULL)
#define c_consume(c, t, err)   c_consume_at((c), (t), (err), VM_NULL, 0, VM_NULL)
#endif
void       c_advance(Compiler *c);
vm_bool    c_check(Compiler *c, TokenType t);
vm_bool    c_match(Compiler *c, TokenType t);
vm_u8      c_alloc_reg(Compiler *c);
void       c_begin_scope(Compiler *c);
void       c_end_scope(Compiler *c);
void       c_emit_defer_entry(Compiler *c, int idx);
void       c_emit_defers_for_return(Compiler *c);
int        c_resolve_local(Compiler *c, const char *name, vm_u32 len);
int        c_resolve_outer_local(Compiler *c, const char *name, vm_u32 len);
int        c_resolve_global_const(Compiler *c, const char *name, vm_u32 len);
vm_u8      c_declare_local(Compiler *c, Token name, VMDataType type, vm_bool is_const, VMReg const_val);
vm_u8      c_emit_load_const(Compiler *c, VMReg val, VMDataType type);

/* Function Overload, Struct Method & Object Slicing Resolution (`scope_func.c`) */
void       c_register_alias(Compiler *c, const char *alias, vm_i32 func_id);
void       c_bind_all_overloads(Compiler *c, const char *target_name, const char *new_alias);
int        c_resolve_struct(Compiler *c, const char *name, vm_u32 len);
int        c_resolve_type_contract(Compiler *c, const char *name, vm_u32 len);
vm_bool    c_does_struct_implement_type(Compiler *c, int sid, int tid);
vm_bool    c_is_struct_compatible(Compiler *c, int child_sid, int expected_sid);
vm_bool    c_can_access_struct_member(Compiler *c, int owner_sid, vm_bool is_pub);
vm_bool    c_check_no_object_slicing(Compiler *c, int dst_sid, int src_sid);
vm_bool    c_check_duplicate_func_overload(Compiler *c, int new_fid);
vm_bool    c_check_foreign_overload(Compiler *c, int new_fid);
int        c_resolve_func(Compiler *c, const char *name, vm_u32 len);
int        c_resolve_func_overload(Compiler *c, const char *name, vm_u32 len,
                                   const VMDataType *arg_types, const int *arg_sids, vm_u8 argc);
int        c_resolve_struct_method_overload(Compiler *c, int sid, const char *mname, vm_u32 mlen,
                                            const VMDataType *arg_types, const int *arg_sids, vm_u8 argc);
int        c_resolve_struct_method(Compiler *c, int sid, const char *mname, vm_u32 mlen);
int        c_resolve_enum_variant(Compiler *c, const char *qname, vm_u32 len);
int        c_resolve_generic(Compiler *c, const char *name, vm_u32 len);
void       c_verify_all_type_contracts(Compiler *c);

/* Type Annotation & `Self` Resolution (`scope_type.c`) */
VMDataType c_parse_type_ex(Compiler *c, int *out_struct_id);
vm_u32     c_alloc_func_slot(Compiler *c);
void       c_func_index_reset(Compiler *c);
int        c_func_first_named(Compiler *c, const char *name, vm_u32 len);          /* lowest fid named `name`, or -1 */
int        c_func_next_named(Compiler *c, int fid, const char *name, vm_u32 len); /* next higher fid with that name */
void       c_func_index_rename(Compiler *c, vm_u32 fid); /* call after changing functions[fid].name */
VMDataType c_parse_type(Compiler *c);
void       c_parse_attributes(Compiler *c);

#endif /* COMPILER_SCOPE_CONTEXT_H */
