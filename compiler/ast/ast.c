#include "ast.h"
#include "../../vm/syscall/vm_syscall.h"
#include "../../vm/string/vm_string.h"

void ast_init(AstTree *t, VMReallocFn fn, void *user_data) {
    t->nodes = VM_NULL;
    t->num_nodes = t->nodes_cap = 0;
    t->extra = VM_NULL;
    t->num_extra = t->extra_cap = 0;
    t->toks = VM_NULL;
    t->num_toks = t->toks_cap = 0;
    t->bufs = VM_NULL;
    t->num_bufs = t->bufs_cap = 0;
    t->spans = VM_NULL;
    t->num_spans = t->spans_cap = 0;
    t->root = 0;
    t->num_errors = 0;
    t->out_of_memory = VM_FALSE;
    t->realloc_fn = (fn != VM_NULL) ? fn : vm_sys_default_realloc;
    t->alloc_ud = user_data;
}

void ast_free(AstTree *t) {
    for (vm_u32 i = 0; i < t->num_bufs; i++) t->realloc_fn(t->alloc_ud, t->bufs[i], 0, 0);
    t->realloc_fn(t->alloc_ud, t->bufs, sizeof(char *) * t->bufs_cap, 0);
    t->realloc_fn(t->alloc_ud, t->nodes, sizeof(AstNode) * t->nodes_cap, 0);
    t->realloc_fn(t->alloc_ud, t->extra, sizeof(vm_u32) * t->extra_cap, 0);
    t->realloc_fn(t->alloc_ud, t->toks, sizeof(Token) * t->toks_cap, 0);
    if (t->spans) t->realloc_fn(t->alloc_ud, t->spans, sizeof(vm_u32) * t->spans_cap, 0);
    ast_init(t, t->realloc_fn, t->alloc_ud);
}

const char *ast_kind_name(vm_u8 kind) {
    switch ((AstKind)kind) {
        case AST_NONE:        return "none";
        case AST_TY_NAME:     return "TyName";
        case AST_TY_ARRAY:    return "TyArray";
        case AST_TY_MAP:      return "TyMap";
        case AST_TY_TUPLE:    return "TyTuple";
        case AST_TY_FUNC:     return "TyFunc";
        case AST_TY_OPTIONAL: return "TyOptional";
        case AST_UNIT:        return "Unit";
        case AST_ATTR:        return "Attr";
        case AST_IMPORT:      return "Import";
        case AST_IMPORT_ITEM: return "ImportItem";
        case AST_FUNC:        return "Func";
        case AST_PARAM:       return "Param";
        case AST_STRUCT:      return "Struct";
        case AST_FIELD:       return "Field";
        case AST_CONTRACT:    return "Contract";
        case AST_TYPE_ALIAS:  return "TypeAlias";
        case AST_ENUM:        return "Enum";
        case AST_VARIANT:     return "Variant";
        case AST_CONST:       return "Const";
        case AST_EXPORT:      return "Export";
        case AST_MODULE:      return "Module";
        case AST_BLOCK:       return "Block";
        case AST_LET:         return "Let";
        case AST_IF:          return "If";
        case AST_WHILE:       return "While";
        case AST_FOR:         return "For";
        case AST_MATCH:       return "Match";
        case AST_ARM:         return "Arm";
        case AST_RETURN:      return "Return";
        case AST_BREAK:       return "Break";
        case AST_CONTINUE:    return "Continue";
        case AST_DEFER:       return "Defer";
        case AST_PRINT:       return "Print";
        case AST_YIELD:       return "Yield";
        case AST_ASSIGN:      return "Assign";
        case AST_EXPR_STMT:   return "ExprStmt";
        case AST_INT:         return "Int";
        case AST_FLOAT:       return "Float";
        case AST_STRING:      return "String";
        case AST_FSTRING:     return "FString";
        case AST_STR_PART:    return "StrPart";
        case AST_BOOL:        return "Bool";
        case AST_IDENT:       return "Ident";
        case AST_UNARY:       return "Unary";
        case AST_BINARY:      return "Binary";
        case AST_RANGE:       return "Range";
        case AST_CALL:        return "Call";
        case AST_METHOD:      return "Method";
        case AST_FIELD_GET:   return "FieldGet";
        case AST_INDEX:       return "Index";
        case AST_ARRAY:       return "Array";
        case AST_MAP:         return "Map";
        case AST_TUPLE:       return "Tuple";
        case AST_TRY:         return "Try";
        case AST_NAMED_ARG:   return "NamedArg";
        case AST_OPT_CHAIN:   return "OptChain";
        case AST_PROPAGATE:   return "Propagate";
        case AST_CATCH:       return "Catch";
        case AST_THROW:       return "Throw";
        default:              return "?";
    }
}

/* ---- Dump (debugging aid: `vml file.vml --ast`) ---- */

static void dump_indent(int depth) {
    for (int i = 0; i < depth; i++) vm_sys_write_cstr("  ");
}

static void dump_num(vm_i64 v) {
    char buf[24];
    vm_u32 len = vm_i64_to_str(buf, v);
    vm_sys_write(buf, len);
}

static void dump_tok(const AstTree *t, vm_u32 tok) {
    const Token *k = &t->toks[tok];
    vm_sys_write(k->start, k->length);
}

static void dump_node(const AstTree *t, AstRef n, int depth, const char *label);

static void dump_list(const AstTree *t, AstList l, int depth, const char *label) {
    vm_u32 n = ast_list_len(t, l);
    if (n == 0) return;
    dump_indent(depth);
    vm_sys_write_cstr(label);
    vm_sys_write_cstr(":\n");
    for (vm_u32 i = 0; i < n; i++) dump_node(t, ast_list_at(t, l, i), depth + 1, VM_NULL);
}

static void dump_tok_list(const AstTree *t, AstList l, int depth, const char *label) {
    vm_u32 n = ast_list_len(t, l);
    if (n == 0) return;
    dump_indent(depth);
    vm_sys_write_cstr(label);
    vm_sys_write_cstr(": ");
    for (vm_u32 i = 0; i < n; i++) {
        if (i > 0) vm_sys_write_cstr(" ");
        dump_tok(t, ast_list_at(t, l, i));
    }
    vm_sys_write_cstr("\n");
}

static void dump_node(const AstTree *t, AstRef n, int depth, const char *label) {
    if (n == 0) return;
    const AstNode *nd = &t->nodes[n];
    dump_indent(depth);
    if (label != VM_NULL) {
        vm_sys_write_cstr(label);
        vm_sys_write_cstr("=");
    }
    vm_sys_write_cstr(ast_kind_name(nd->kind));

    switch ((AstKind)nd->kind) {
        case AST_TY_NAME: case AST_ATTR: case AST_IMPORT_ITEM: case AST_PARAM: case AST_STRUCT:
        case AST_FIELD: case AST_CONTRACT: case AST_TYPE_ALIAS: case AST_ENUM: case AST_VARIANT:
        case AST_CONST: case AST_INT: case AST_FLOAT: case AST_STRING: case AST_FSTRING:
        case AST_IDENT: case AST_METHOD: case AST_FIELD_GET: case AST_NAMED_ARG: case AST_OPT_CHAIN:
            vm_sys_write_cstr(" ");
            dump_tok(t, nd->tok);
            break;
        case AST_FUNC:
            vm_sys_write_cstr(" ");
            if ((nd->flags & AST_F_LAMBDA) && !(nd->flags & AST_F_NAMED)) {
                vm_sys_write_cstr("<lambda>");
                break;
            }
            for (vm_u32 i = 0; i < (nd->op ? nd->op : 1u); i++) dump_tok(t, nd->tok + i);
            break;
        case AST_UNARY: case AST_BINARY: case AST_ASSIGN:
            vm_sys_write_cstr(" op=");
            dump_num((vm_i64)nd->op);
            break;
        case AST_BOOL:
            vm_sys_write_cstr(nd->op ? " true" : " false");
            break;
        case AST_TRY:
            if (nd->tok != 0) {
                vm_sys_write_cstr(" err=");
                dump_tok(t, nd->tok);
            }
            break;
        default:
            break;
    }
    if (nd->flags != 0) {
        vm_sys_write_cstr(" flags=");
        dump_num((vm_i64)nd->flags);
    }
    vm_sys_write_cstr("\n");

    int d1 = depth + 1;
    switch ((AstKind)nd->kind) {
        case AST_TY_NAME:     dump_list(t, nd->a, d1, "args"); break;
        case AST_TY_ARRAY:    dump_node(t, nd->a, d1, "elem"); break;
        case AST_TY_MAP:      dump_node(t, nd->a, d1, "key"); dump_node(t, nd->b, d1, "val"); break;
        case AST_TY_TUPLE:    dump_list(t, nd->a, d1, "elems"); break;
        case AST_TY_FUNC:     dump_list(t, nd->a, d1, "params"); dump_node(t, nd->b, d1, "ret"); break;
        case AST_TY_OPTIONAL: dump_node(t, nd->a, d1, "inner"); break;
        case AST_UNIT:        dump_list(t, nd->a, d1, "items"); break;
        case AST_ATTR:        dump_list(t, nd->a, d1, "args"); break;
        case AST_IMPORT:
            dump_tok_list(t, nd->a, d1, "path");
            dump_list(t, nd->b, d1, "items");
            if (nd->c != 0) { dump_indent(d1); vm_sys_write_cstr("as "); dump_tok(t, nd->c); vm_sys_write_cstr("\n"); }
            break;
        case AST_IMPORT_ITEM:
            if (nd->a != 0) { dump_indent(d1); vm_sys_write_cstr("as "); dump_tok(t, nd->a); vm_sys_write_cstr("\n"); }
            break;
        case AST_FUNC:
            dump_tok_list(t, AST_FUNC_OWNER(t, n), d1, "owner");
            dump_tok_list(t, AST_FUNC_GENERICS(t, n), d1, "generics");
            dump_list(t, AST_FUNC_ATTRS(t, n), d1, "attrs");
            dump_list(t, nd->a, d1, "params");
            dump_node(t, nd->c, d1, "ret");
            dump_node(t, nd->b, d1, "body");
            break;
        case AST_PARAM:       dump_node(t, nd->a, d1, "type"); dump_node(t, nd->b, d1, "default"); break;
        case AST_STRUCT:
            dump_tok_list(t, nd->a, d1, "generics");
            dump_list(t, nd->b, d1, "parents");
            dump_list(t, nd->d, d1, "attrs");
            dump_list(t, nd->c, d1, "members");
            break;
        case AST_FIELD:       dump_node(t, nd->a, d1, "type"); dump_node(t, nd->b, d1, "default"); break;
        case AST_CONTRACT:    dump_list(t, nd->c, d1, "members"); break;
        case AST_TYPE_ALIAS:  dump_node(t, nd->a, d1, "type"); break;
        case AST_ENUM:        dump_list(t, nd->a, d1, "variants"); break;
        case AST_VARIANT:     dump_list(t, nd->a, d1, "payload"); break;
        case AST_CONST:       dump_node(t, nd->a, d1, "type"); dump_node(t, nd->b, d1, "value"); break;
        case AST_EXPORT:      dump_list(t, nd->a, d1, "items"); break;
        case AST_MODULE:      dump_list(t, nd->a, d1, "path"); break;
        case AST_BLOCK:       dump_list(t, nd->a, d1, "stmts"); break;
        case AST_LET:
            dump_tok_list(t, nd->a, d1, "names");
            dump_node(t, nd->b, d1, "type");
            dump_node(t, nd->c, d1, "init");
            break;
        case AST_IF:
            dump_node(t, nd->d, d1, "init");
            dump_node(t, nd->a, d1, "cond");
            dump_node(t, nd->b, d1, "then");
            dump_node(t, nd->c, d1, "else");
            break;
        case AST_WHILE:
            dump_node(t, nd->d, d1, "init");
            dump_node(t, nd->a, d1, "cond");
            dump_node(t, nd->b, d1, "body");
            break;
        case AST_FOR:
            dump_tok_list(t, nd->a, d1, "vars");
            dump_node(t, nd->b, d1, "iter");
            dump_node(t, nd->c, d1, "body");
            break;
        case AST_MATCH:       dump_node(t, nd->a, d1, "subject"); dump_list(t, nd->b, d1, "arms"); break;
        case AST_ARM:         dump_node(t, nd->a, d1, "pattern"); dump_node(t, nd->b, d1, "body"); break;
        case AST_RETURN:      dump_list(t, nd->a, d1, "values"); break;
        case AST_DEFER: case AST_PRINT: case AST_YIELD: case AST_EXPR_STMT: case AST_UNARY:
            dump_node(t, nd->a, d1, VM_NULL);
            break;
        case AST_ASSIGN:      dump_list(t, nd->a, d1, "targets"); dump_list(t, nd->b, d1, "values"); break;
        case AST_FSTRING:     dump_list(t, nd->a, d1, "parts"); break;
        case AST_BINARY: case AST_RANGE: case AST_INDEX:
            dump_node(t, nd->a, d1, VM_NULL);
            dump_node(t, nd->b, d1, VM_NULL);
            break;
        case AST_CALL:
            dump_node(t, nd->a, d1, "callee");
            dump_list(t, nd->c, d1, "type_args");
            dump_list(t, nd->b, d1, "args");
            break;
        case AST_METHOD:      dump_node(t, nd->a, d1, "recv"); dump_list(t, nd->b, d1, "args"); break;
        case AST_FIELD_GET:   dump_node(t, nd->a, d1, "obj"); break;
        case AST_ARRAY: case AST_TUPLE: dump_list(t, nd->a, d1, "elems"); break;
        case AST_MAP:         dump_list(t, nd->a, d1, "keys"); dump_list(t, nd->b, d1, "vals"); break;
        case AST_TRY:         dump_node(t, nd->a, d1, "expr"); dump_node(t, nd->b, d1, "fallback"); break;
        case AST_NAMED_ARG:   dump_node(t, nd->a, d1, "value"); break;
        case AST_OPT_CHAIN:   dump_node(t, nd->a, d1, "obj"); dump_list(t, nd->b, d1, "args"); break;
        case AST_PROPAGATE:   dump_node(t, nd->a, d1, "expr"); break;
        case AST_THROW:       dump_node(t, nd->a, d1, "error"); break;
        case AST_CATCH:       dump_node(t, nd->a, d1, "expr"); dump_node(t, nd->b, d1, "fallback"); break;
        default: break;
    }
}

void ast_dump(const AstTree *t) {
    dump_node(t, t->root, 0, VM_NULL);
}

void ast_print_errors(const AstTree *t, const char *file_name) {
    for (vm_u32 i = 0; i < t->num_errors; i++) {
        vm_sys_write_cstr("[Syntax Error ");
        vm_sys_write_cstr(file_name);
        vm_sys_write_cstr(":");
        char lbuf[24];
        vm_u32 llen = vm_i64_to_str(lbuf, (vm_i64)t->errors[i].line);
        vm_sys_write(lbuf, llen);
        vm_sys_write_cstr("] ");
        vm_sys_write_cstr(t->errors[i].msg);
        vm_sys_write_cstr("\n");
    }
}
