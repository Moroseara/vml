#ifndef COMPILER_GEN_H
#define COMPILER_GEN_H

#include "../scope/context.h"
#include "../ast/ast.h"

/*
 * ============================================================================
 * AST CODE GENERATOR (`compiler/gen/`)
 * Walks the AST and emits bytecode. Symbol tables (structs, functions,
 * contracts, overload resolution) are shared with the legacy compiler through
 * `Compiler` and the `scope_*.c` helpers, so both front ends produce the same
 * runtime layout.
 * ============================================================================
 */

#define GEN_MAX_DEFERS   32
#define GEN_MAX_TREES    32

typedef struct {
    AstTree *t;
    AstRef   node;      /* statement or block to run at scope exit */
    int      depth;
} GenDefer;

typedef struct {
    AstTree *t;
    AstRef   fn;        /* AST_FUNC of a generic template (indexed like Compiler.generics) */
    char     prefix[64];/* namespace it was declared in ("" / "mod." / "Struct") */
    int      struct_id; /* owning struct for generic methods, or -1 */
} GenTemplate;

/* One compiled instance of a generic function: `name<T...>` */
typedef struct {
    int        gen_id;
    vm_u8      num_types;
    VMDataType types[8];
    int        sids[8];
    int        fid;
} GenInstance;

#define GEN_MAX_INSTANCES 256

/* Saved compiler state around a nested function compile */
typedef struct {
    LocalVar   locals[256];
    int        num_locals;
    LocalVar   outer[128];
    int        num_outer;
    int        scope_depth;
    vm_u8      next_reg, max_reg;
    VMDataType ret_type;
    int        ret_sid;
    int        func_id;
    int        num_defers;
    int        num_breaks, num_continues;
} FuncState;

#define GEN_MAX_FN_DEPTH  16
#define GEN_MAX_CAPTURES  64

typedef struct {
    const char *name;
    vm_u32      len;
    vm_u8       kind;        /* VM_UPV_CELL / VM_UPV_SNAPSHOT / VM_UPV_PARENT */
    vm_u8       index;       /* enclosing register, or enclosing function's upvalue index */
    LocalVar    origin;      /* the captured variable as declared (type, lambda info) */
} GenUpval;

/* One entry per function being compiled (innermost last) */
typedef struct {
    FuncState  *enclosing;   /* enclosing function's locals at the lambda's position (NULL = top level) */
    int         fid;
    GenUpval    upv[VM_MAX_UPVALUES];
    int         num_upv;
    /* Names used inside nested functions: locals with these names get boxed */
    const char *cap_name[GEN_MAX_CAPTURES];
    vm_u32      cap_len[GEN_MAX_CAPTURES];
    int         num_cap;
    vm_bool     cap_overflow;
} GenFn;

typedef struct Gen {
    Compiler   *c;
    AstTree    *t;                          /* tree currently being compiled */

    GenDefer    defers[GEN_MAX_DEFERS];
    int         num_defers;

    GenTemplate templates[64];
    GenInstance instances[GEN_MAX_INSTANCES];
    vm_bool     proto_pass;                 /* gen_unit pass 1: declare function signatures only */
    int         num_instances;
    /* `type func` declarations: each must get a body with the same signature before compiling ends */
    vm_u32      decl_fid[128];
    vm_u16      decl_line[128];
    Token       decl_tok[128];
    vm_u8       decl_file[128];
    int         num_decls;

    /* Default method bodies declared inside `type` headers: [type contract][method] */
    AstTree    *contract_tree[32];
    AstRef      contract_body[32][16];

    GenFn       fns[GEN_MAX_FN_DEPTH];
    int         fn_depth;

    /* Parameter names of compiled functions (named arguments): [function id][param] */
    const char *param_name[VM_MAX_FUNCTIONS][VM_MAX_PARAMS];
    VMDataType  ret_elem[VM_MAX_FUNCTIONS];
    const struct VMIntrinsicS *inline_intr[VM_MAX_FUNCTIONS];  /* opcode intrinsic: calls are inlined */   /* element type of a `T[]` return (VOID = unknown) */
    vm_u32      param_len[VM_MAX_FUNCTIONS][VM_MAX_PARAMS];

    /* Function type a trailing block `{ a -> ... }` must take (-1 = unknown) */
    int         block_ftid;

    /* Element type an array literal should build (`let xs: i32[] = [...]`), GEN_ANY = none */
    VMDataType  elem_hint;

    /* `try ... or err { => value }`: where `=>` (AST_YIELD) writes its value */
    vm_bool     yield_active;
    vm_u8       yield_dst;
    VMDataType  yield_type;

    /* Imported module trees stay alive until compilation ends (names point into them) */
    AstTree    *trees[GEN_MAX_TREES];
    int         num_trees;
} Gen;

/* "No expected type" for gen_expr: integer literals then default to `int` (i64) */
#define GEN_ANY ((VMDataType)VM_TYPE_COUNT)

/* ---- entry point ---- */
vm_bool gen_compile_source(const char *source, VMChunk *chunk, const VMConfig *cfg);

/* ---- shared helpers (gen_util.c) ---- */
const Token *gen_tok(const Gen *g, vm_u32 tok);
Token        gen_token(const Gen *g, vm_u32 tok);
vm_u32       gen_node_tok(const Gen *g, AstRef n);
void         gen_at(Gen *g, AstRef n);
void         gen_error(Gen *g, AstRef n, const char *msg);
vm_bool      gen_tok_is(const Gen *g, vm_u32 tok, const char *text);
ExprResult   gen_result(VMDataType type);
VMDataType   gen_type(Gen *g, AstRef ty, int *out_sid, VMDataType *out_elem, int *out_ftid);
int          gen_func_type(Gen *g, AstRef ty_func);
vm_u8        gen_struct_width(Gen *g, ExprResult r);
void         gen_patch_jump(Gen *g, vm_u32 at);
void         gen_patch_jump_to(Gen *g, vm_u32 at, vm_u32 target);
vm_u32       gen_emit_jump(Gen *g, VMOpCode op, vm_u8 a);
void         gen_emit_jump_back(Gen *g, VMOpCode op, vm_u8 a, vm_u32 target);
vm_u8        gen_move_into(Gen *g, ExprResult r, vm_u8 dst);
ExprResult   gen_call_fid(Gen *g, int fid, const ExprResult *args, vm_u8 argc);
ExprResult   gen_call_value(Gen *g, int fid, const ExprResult *args, vm_u8 argc, int fn_reg);
vm_u8        gen_to_string_reg(Gen *g, ExprResult val);
void         gen_set_res_from_fn(Gen *g, ExprResult *res, int fid, vm_u8 base);
vm_u32       gen_const_string(Gen *g, const char *s, vm_u32 len);
void         gen_quoted_msg(char *out, vm_u32 cap, const char *prefix, const char *name, vm_u32 nlen);
vm_bool      gen_is_numeric(VMDataType t);
VMDataType   gen_wider(VMDataType a, VMDataType b);
ExprResult   gen_convert(Gen *g, ExprResult r, VMDataType to);

/* ---- closures (gen_closure.c) ---- */
void         gen_fn_push(Gen *g, FuncState *enclosing, int fid, AstRef body);
void         gen_fn_pop(Gen *g);
vm_bool      gen_should_box(Gen *g, const char *name, vm_u32 len);
void         gen_box_local(Gen *g, int loc);
vm_u8        gen_local_value(Gen *g, int loc);
int          gen_resolve_upvalue(Gen *g, const char *name, vm_u32 len);
ExprResult   gen_upvalue_get(Gen *g, int idx);
void         gen_finish_closure_info(Gen *g, int fid);

/* ---- optionals (gen_optional.c) ---- */
vm_bool      gen_is_optional(Gen *g, ExprResult r);
vm_bool      gen_sid_is_optional(Gen *g, int sid);
int          gen_optional_sid(Gen *g, VMDataType inner, int inner_sid, AstRef at);
ExprResult   gen_wrap_optional(Gen *g, ExprResult r, int sid);
ExprResult   gen_coerce(Gen *g, ExprResult r, VMDataType type, int sid, AstRef at);
ExprResult   gen_none(Gen *g);
ExprResult   gen_coalesce(Gen *g, AstRef n, VMDataType expected);
ExprResult   gen_opt_chain(Gen *g, AstRef n);
vm_u8        gen_optional_to_string(Gen *g, ExprResult r);

/* ---- enums & match (gen_enum.c) ---- */
vm_bool      gen_is_enum(Gen *g, ExprResult r);
ExprResult   gen_enum_value(Gen *g, AstRef at, int ev, AstList args);
ExprResult   gen_match_any(Gen *g, AstRef n, vm_bool want_value, VMDataType expected);
vm_u8        gen_enum_to_string(Gen *g, ExprResult r);

ExprResult   gen_block_value(Gen *g, AstRef block, VMDataType expected, vm_bool *has_value);

/* ---- errors as values `T ! E` (gen_error.c) ---- */
vm_bool      gen_is_result(Gen *g, ExprResult r);
vm_bool      gen_sid_is_result(Gen *g, int sid);
int          gen_result_sid(Gen *g, VMDataType vt, int vsid, VMDataType et, int esid, AstRef at);
ExprResult   gen_wrap_ok(Gen *g, ExprResult r, int sid, AstRef at);
ExprResult   gen_ok_void(Gen *g, int sid);
void         gen_throw(Gen *g, AstRef n);
ExprResult   gen_propagate(Gen *g, AstRef n);
ExprResult   gen_catch(Gen *g, AstRef n, VMDataType expected);

/* ---- module interface (gen_interface.c) ---- */
void         gen_build_interface(Gen *g, const AstTree *tree, const char *file_name);

/* ---- expressions (gen_expr.c) ---- */
ExprResult   gen_method_on(Gen *g, AstRef at, ExprResult self_expr, const char *mname, vm_u32 mlen, AstList args);
ExprResult   gen_expr(Gen *g, AstRef n, VMDataType expected);
void         gen_print(Gen *g, AstRef arg);

/* ---- statements (gen_stmt.c) ---- */
void         gen_block(Gen *g, AstRef block);
void         gen_block_scoped(Gen *g, AstRef block);
void         gen_stmt(Gen *g, AstRef n);
void         gen_begin_scope(Gen *g);
void         gen_end_scope(Gen *g);
void         gen_emit_defers_for_return(Gen *g);

/* ---- declarations (gen_decl.c) ---- */
int          gen_function(Gen *g, AstRef fn_node, const char *prefix, const char *custom_name, vm_bool is_pub);
ExprResult   gen_lambda(Gen *g, AstRef fn_node);
int          gen_instantiate_generic(Gen *g, int gen_id, const VMDataType *types, const int *sids, vm_u8 num_types, AstRef at);
ExprResult   gen_infer_generic_call(Gen *g, AstRef call, int gen_id, AstList args);
void         gen_unit(Gen *g, AstRef unit);
const char  *gen_operator_name(Gen *g, AstRef fn_node);

#endif /* COMPILER_GEN_H */
