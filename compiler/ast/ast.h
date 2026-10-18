#ifndef COMPILER_AST_H
#define COMPILER_AST_H

#include "../lexer/token.h"
#include "../../vm/memory/vm_memory.h"

/*
 * ============================================================================
 * ABSTRACT SYNTAX TREE (`compiler/ast/ast.h`)
 * Flat, index-based tree: every node lives in one growable array and refers to
 * children by 32-bit index (0 = none). Variable-length children are "lists":
 * a list is an index into `extra[]` where extra[list] = count, followed by the
 * items. List 0 is the shared empty list. Freed in one shot with `ast_free`.
 * ============================================================================
 */

typedef vm_u32 AstRef;   /* node index, 0 = none */
typedef vm_u32 AstList;  /* index into extra[], 0 = empty list */

typedef enum {
    AST_NONE = 0,

    /* ---- Types ---- */
    AST_TY_NAME,      /* tok = name (ident / builtin keyword / Self); a = generic arg list; flags: AST_F_VARIADIC */
    AST_TY_ARRAY,     /* a = element type */
    AST_TY_MAP,       /* a = key type, b = value type */
    AST_TY_TUPLE,     /* a = list of types */
    AST_TY_FUNC,      /* a = list of AST_PARAM, b = return type (0 = void) */
    AST_TY_OPTIONAL,  /* a = inner type */
    AST_TY_RESULT,    /* `T ! E`: a = value type, b = error type */

    /* ---- Items / declarations ---- */
    AST_UNIT,         /* a = list of items */
    AST_ATTR,         /* tok = name; a = list of argument exprs */
    AST_IMPORT,       /* a = list of path token indices; b = list of AST_IMPORT_ITEM; c = alias token (0 = none); flags: AST_F_BRACES */
    AST_MODULE,       /* `module a.b`: a = list of path token indices */
    AST_IMPORT_ITEM,  /* tok = name or `*`; a = alias token (0 = none) */
    AST_FUNC,         /* tok = name token (operator: first operator token); op = operator token count;
                         a = list of AST_PARAM; b = body block (0 = prototype); c = return type;
                         d = AstFuncExtra index (owner path, generics, attrs) */
    AST_PARAM,        /* tok = name; a = type (0 = inferred); b = default expr; flags: AST_F_VARIADIC */
    AST_STRUCT,       /* tok = name; a = list of generic param tokens; b = list of parent types; c = list of members; d = attrs list */
    AST_FIELD,        /* tok = name; a = type; b = default expr */
    AST_CONTRACT,     /* `type Name { ... }`: tok = name; c = list of members (AST_FIELD / AST_FUNC prototypes) */
    AST_TYPE_ALIAS,   /* `type name = T`: tok = name; a = type */
    AST_ENUM,         /* tok = name; a = list of AST_VARIANT; b = `as ns` alias token (0 = none) */
    AST_VARIANT,      /* tok = name; a = list of payload types; b = explicit `= value` expr */
    AST_CONST,        /* tok = name; a = type; b = value; c = `as ns` alias token (0 = none) */
    AST_EXPORT,       /* `export Name as alias` / `export { a as b }`: a = list of AST_IMPORT_ITEM (tok = original, a = exported name) */

    /* ---- Statements ---- */
    AST_BLOCK,        /* a = list of statements */
    AST_LET,          /* a = list of name tokens; b = type; c = init expr; flags: AST_F_CONST */
    AST_IF,           /* a = cond; b = then block; c = else (block or AST_IF); d = init AST_LET (`if let`); flags: AST_F_CONST */
    AST_WHILE,        /* a = cond; b = body; d = init AST_LET (`while let`); flags: AST_F_CONST */
    AST_FOR,          /* a = list of loop var tokens; b = iterable; c = body */
    AST_MATCH,        /* a = subject; b = list of AST_ARM */
    AST_ARM,          /* a = pattern expr (0 = `_`); b = body (block or expr) */
    AST_RETURN,       /* a = list of values */
    AST_BREAK,
    AST_CONTINUE,
    AST_DEFER,        /* a = statement or block */
    AST_PRINT,        /* a = expr */
    AST_YIELD,        /* `=> expr` inside a block: block value; a = expr */
    AST_ASSIGN,       /* op = TokenType (=, +=, ...); a = list of targets; b = list of values */
    AST_THROW,        /* `throw expr`: a = error value */
    AST_EXPR_STMT,    /* a = expr */

    /* ---- Expressions ---- */
    AST_INT,          /* tok = literal token */
    AST_FLOAT,        /* tok = literal token */
    AST_STRING,       /* tok = literal token (raw text, escapes not yet processed) */
    AST_FSTRING,      /* tok = literal token; a = list of parts (AST_STR_PART / exprs) */
    AST_STR_PART,     /* a = byte offset into the f-string token text; b = length */
    AST_BOOL,         /* op = 0 / 1 */
    AST_IDENT,        /* tok = name (also `len`, `Self`) */
    AST_UNARY,        /* op = TokenType; a = operand */
    AST_BINARY,       /* op = TokenType; a = lhs; b = rhs (for `as` / `is`: b = type node or string) */
    AST_RANGE,        /* a = lo; b = hi */
    AST_CALL,         /* a = callee; b = list of args; c = list of generic type args */
    AST_METHOD,       /* tok = method name; a = receiver; b = list of args  (`x:m(...)`) */
    AST_FIELD_GET,    /* tok = field name; a = object */
    AST_INDEX,        /* a = object; b = index */
    AST_ARRAY,        /* a = list of elements */
    AST_MAP,          /* a = list of keys; b = list of values */
    AST_TUPLE,        /* a = list of elements */
    AST_TRY,          /* a = expr; b = fallback (expr or block); tok = error binding name (0 = none); flags: AST_F_TRY_KW */
    AST_NAMED_ARG,    /* `name = value` inside `f { ... }`: tok = name; a = value */
    AST_PROPAGATE,    /* `expr?`: a = expr (a `T ! E` result) */
    AST_CATCH,        /* `expr catch fallback` / `expr catch e { ... }`: a = expr; b = fallback; tok = binding (0 = none) */
    AST_OPT_CHAIN,    /* `obj?.name` / `obj?.name(args)`: tok = name; a = obj; b = args; flags: AST_F_CALL_ARGS */

    AST_KIND_COUNT
} AstKind;

/* Node flags */
#define AST_F_PUB        0x0001u
#define AST_F_EXPORT     0x0002u
#define AST_F_CONST      0x0004u
#define AST_F_VARIADIC   0x0008u
#define AST_F_BRACES     0x0010u
#define AST_F_PATH       0x0020u  /* AST_IMPORT written with a file path (`import "./x"`): a = [string token] */
#define AST_F_OPERATOR   0x0020u  /* AST_FUNC is an `operator` */
#define AST_F_LAMBDA     0x0040u  /* AST_FUNC used as an expression */
#define AST_F_PROTO      0x0080u  /* `type func` prototype / signature without body */
#define AST_F_TYPE_HDR   0x0100u  /* `type struct` header */
#define AST_F_TRY_KW     0x0200u  /* AST_TRY written with leading `try` */
#define AST_F_NESTED     0x0400u  /* AST_FUNC declared inside a function body */
#define AST_F_LET        0x0800u  /* AST_IF / AST_WHILE with `let` init */
#define AST_F_NAMED      0x1000u  /* AST_FUNC lambda that has its own name (`func name(...) {}` as expression) */
#define AST_F_BLOCK      0x2000u  /* AST_FUNC trailing block `{ a, b -> ... }`: parameter types come from the callee */
#define AST_F_NAMED_ARGS 0x4000u  /* AST_CALL whose args are AST_NAMED_ARG (`f { a = 1 }`) */
#define AST_F_COMMAND    0x8000u  /* AST_CALL written as a command call (`f a, b`) */
#define AST_F_CALL_ARGS  0x0001u  /* AST_OPT_CHAIN with `( ... )` (a method call) */
#define AST_F_MATCH_EXPR 0x0001u  /* AST_MATCH used as an expression */

typedef struct {
    vm_u8  kind;
    vm_u8  op;
    vm_u16 flags;
    vm_u32 tok;     /* token index (line info, names, literals) */
    vm_u32 a, b, c, d;
} AstNode;          /* 24 bytes */

/* AST_FUNC.d points at 3 consecutive extra[] slots: owner path list, generic param list, attr list */
#define AST_FUNC_OWNER(t, n)    ((t)->extra[(t)->nodes[n].d + 0])
#define AST_FUNC_GENERICS(t, n) ((t)->extra[(t)->nodes[n].d + 1])
#define AST_FUNC_ATTRS(t, n)    ((t)->extra[(t)->nodes[n].d + 2])

typedef struct {
    vm_u32 line;
    char   msg[96];
} AstError;

#define AST_MAX_ERRORS 16

typedef struct {
    AstNode    *nodes;
    vm_u32      num_nodes, nodes_cap;
    vm_u32     *extra;
    vm_u32      num_extra, extra_cap;
    Token      *toks;
    vm_u32      num_toks, toks_cap;
    char      **bufs;           /* owned copies (f-string sub-sources) */
    vm_u32      num_bufs, bufs_cap;
    vm_u32     *spans;          /* per top-level item: node, first token, end token (exclusive) */
    vm_u32      num_spans, spans_cap;
    AstRef      root;
    AstError    errors[AST_MAX_ERRORS];
    vm_u32      num_errors;
    vm_bool     out_of_memory;
    VMReallocFn realloc_fn;
    void       *alloc_ud;
} AstTree;

void        ast_init(AstTree *t, VMReallocFn fn, void *user_data);
void        ast_free(AstTree *t);
const char *ast_kind_name(vm_u8 kind);

static inline vm_u32 ast_list_len(const AstTree *t, AstList l)            { return t->extra[l]; }
static inline vm_u32 ast_list_at(const AstTree *t, AstList l, vm_u32 i)   { return t->extra[l + 1 + i]; }

/* Parses a whole source file. Returns VM_TRUE when there were no syntax errors. */
vm_bool     ast_parse(AstTree *t, const char *src);
void        ast_dump(const AstTree *t);
void        ast_print_errors(const AstTree *t, const char *file_name);

#endif /* COMPILER_AST_H */
