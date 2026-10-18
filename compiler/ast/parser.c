#include "ast.h"
#include "../lexer/lexer.h"
#include "../../vm/string/vm_string.h"

/*
 * ============================================================================
 * PARSER (`compiler/ast/parser.c`)
 * Source -> token array -> AST. Pure syntax: no types, scopes or codegen.
 *
 * Error recovery: the first error inside a top-level item puts the parser in
 * "panic" mode, where every lookahead reports EOF so all loops unwind quickly.
 * The item loop then clears panic and skips to the next top-level keyword.
 * ============================================================================
 */

#define P_MAX_DEPTH 512

typedef struct {
    AstTree *t;
    vm_u32   pos;        /* current token index */
    vm_u32   end;        /* index of this stream's EOF token */
    vm_bool  panic;
    vm_bool  no_brace;   /* `{` ends an expression (if/while/for/match heads) */
    vm_bool  no_method;  /* `:` is not a method call (map literal keys) */
    vm_bool  in_command; /* parsing command-call args: spaced `(` / `{` belong to the command, not the arg */
    vm_u32   depth;
    vm_u32  *stack;      /* scratch for building lists */
    vm_u32   sp, stack_cap;
} Parser;

/* ---------------------------------------------------------------------------
 * Storage
 * ------------------------------------------------------------------------- */

static vm_bool grow(AstTree *t, void **ptr, vm_u32 *cap, vm_u32 need, vm_u32 elem, vm_u32 init) {
    if (need <= *cap) return VM_TRUE;
    vm_u32 nc = (*cap > 0) ? *cap * 2 : init;
    while (nc < need) nc *= 2;
    void *np = t->realloc_fn(t->alloc_ud, *ptr, (vm_size)elem * *cap, (vm_size)elem * nc);
    if (np == VM_NULL) {
        t->out_of_memory = VM_TRUE;
        return VM_FALSE;
    }
    *ptr = np;
    *cap = nc;
    return VM_TRUE;
}

static AstRef new_node(Parser *p, vm_u8 kind, vm_u32 tok) {
    AstTree *t = p->t;
    if (grow(t, (void **)&t->nodes, &t->nodes_cap, t->num_nodes + 1, sizeof(AstNode), 1024) == VM_FALSE) return 0;
    AstRef n = t->num_nodes++;
    AstNode *nd = &t->nodes[n];
    nd->kind = kind;
    nd->op = 0;
    nd->flags = 0;
    nd->tok = tok;
    nd->a = nd->b = nd->c = nd->d = 0;
    return n;
}

#define N(p, n) (&(p)->t->nodes[(n)])

static void push(Parser *p, vm_u32 v) {
    if (grow(p->t, (void **)&p->stack, &p->stack_cap, p->sp + 1, sizeof(vm_u32), 256) == VM_FALSE) return;
    p->stack[p->sp++] = v;
}

/* Moves scratch items [mark, sp) into extra[] as a list */
static AstList finish_list(Parser *p, vm_u32 mark) {
    vm_u32 count = p->sp - mark;
    if (count == 0) return 0;
    AstTree *t = p->t;
    if (grow(t, (void **)&t->extra, &t->extra_cap, t->num_extra + count + 1, sizeof(vm_u32), 1024) == VM_FALSE) {
        p->sp = mark;
        return 0;
    }
    AstList l = t->num_extra;
    t->extra[t->num_extra++] = count;
    for (vm_u32 i = 0; i < count; i++) t->extra[t->num_extra++] = p->stack[mark + i];
    p->sp = mark;
    return l;
}

static vm_u32 alloc_extra(Parser *p, vm_u32 count) {
    AstTree *t = p->t;
    if (grow(t, (void **)&t->extra, &t->extra_cap, t->num_extra + count, sizeof(vm_u32), 1024) == VM_FALSE) return 0;
    vm_u32 at = t->num_extra;
    for (vm_u32 i = 0; i < count; i++) t->extra[t->num_extra++] = 0;
    return at;
}

static vm_bool push_token(AstTree *t, Token tok) {
    if (grow(t, (void **)&t->toks, &t->toks_cap, t->num_toks + 1, sizeof(Token), 4096) == VM_FALSE) return VM_FALSE;
    t->toks[t->num_toks++] = tok;
    return VM_TRUE;
}

/* Lexes `src` and appends its tokens plus a trailing EOF. Returns the index of the first token. */
static vm_u32 lex_into(AstTree *t, const char *src, vm_u32 first_line) {
    Lexer lex;
    lexer_init(&lex, src);
    lex.line = first_line;
    vm_u32 first = t->num_toks;
    for (;;) {
        Token tok = lexer_next(&lex);
        if (push_token(t, tok) == VM_FALSE) break;
        if (tok.type == TOK_EOF) break;
    }
    return first;
}

/* ---------------------------------------------------------------------------
 * Token cursor
 * ------------------------------------------------------------------------- */

static inline TokenType peek_at(const Parser *p, vm_u32 k) {
    if (p->panic) return TOK_EOF;
    vm_u32 i = p->pos + k;
    if (i > p->end) i = p->end;
    return p->t->toks[i].type;
}

static inline TokenType cur(const Parser *p)            { return peek_at(p, 0); }
static inline vm_bool   check(const Parser *p, TokenType ty) { return vm_bool_from_int(cur(p) == ty); }
static inline const Token *tok_at(const Parser *p, vm_u32 i) { return &p->t->toks[i]; }

static vm_u32 advance(Parser *p) {
    vm_u32 i = p->pos;
    if (p->pos < p->end) p->pos++;
    return i;
}

static vm_bool match(Parser *p, TokenType ty) {
    if (cur(p) != ty) return VM_FALSE;
    advance(p);
    return VM_TRUE;
}

static void error_at(Parser *p, vm_u32 tok, const char *msg) {
    if (p->panic) return;
    AstTree *t = p->t;
    if (t->num_errors < AST_MAX_ERRORS) {
        AstError *e = &t->errors[t->num_errors++];
        e->line = t->toks[tok].line;
        vm_u32 n = vm_str_len(msg);
        vm_u32 k = 0;
        for (; k < n && k < sizeof(e->msg) - 24; k++) e->msg[k] = msg[k];
        const Token *near = &t->toks[tok];
        if (near->type != TOK_EOF && near->length > 0) {
            const char *pfx = " (near '";
            for (vm_u32 i = 0; pfx[i]; i++) e->msg[k++] = pfx[i];
            for (vm_u32 i = 0; i < near->length && i < 12; i++) e->msg[k++] = near->start[i];
            e->msg[k++] = '\'';
            e->msg[k++] = ')';
        }
        e->msg[k] = '\0';
    }
    p->panic = VM_TRUE;
}

static void error(Parser *p, const char *msg) {
    error_at(p, p->pos, msg);
}

static vm_u32 expect(Parser *p, TokenType ty, const char *msg) {
    if (cur(p) == ty) return advance(p);
    error(p, msg);
    return p->pos;
}

/* Identifiers, plus keywords that are valid as names after `.` / in import paths (`std.string`) */
static vm_bool is_name_type(TokenType ty) {
    return vm_bool_from_int(ty == TOK_IDENT || (ty >= TOK_FUNC && ty <= TOK_TYPE_VOID));
}

static vm_u32 expect_name(Parser *p, const char *msg) {
    if (is_name_type(cur(p)) == VM_TRUE) return advance(p);
    error(p, msg);
    return p->pos;
}

/* No whitespace between the previous token and the current one (`b()` vs `b ()`) */
static vm_bool adjacent(const Parser *p) {
    if (p->panic || p->pos == 0) return VM_FALSE;
    const Token *prev = &p->t->toks[p->pos - 1];
    const Token *cur_t = &p->t->toks[p->pos];
    return vm_bool_from_int(prev->start + prev->length == cur_t->start);
}

static vm_bool same_line(const Parser *p) {
    if (p->panic || p->pos == 0) return VM_FALSE;
    return vm_bool_from_int(p->t->toks[p->pos - 1].line == p->t->toks[p->pos].line);
}

static vm_bool tok_is(const Parser *p, vm_u32 i, const char *text) {
    const Token *k = tok_at(p, i);
    return vm_bool_from_int(k->type == TOK_IDENT && vm_str_eq_cstr(k->start, k->length, text) == VM_TRUE);
}

static vm_bool cur_is_word(const Parser *p, const char *text) {
    if (p->panic) return VM_FALSE;
    return tok_is(p, p->pos, text);
}

static vm_bool enter(Parser *p) {
    if (++p->depth > P_MAX_DEPTH) {
        error(p, "Nesting too deep");
        return VM_FALSE;
    }
    return VM_TRUE;
}

static inline void leave(Parser *p) { p->depth--; }

/* ---------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */

static AstRef parse_expr(Parser *p);
static AstRef parse_type(Parser *p);
static AstRef parse_block(Parser *p);
static AstRef parse_stmt(Parser *p);
static AstRef parse_func(Parser *p, vm_u16 flags, AstList attrs, vm_bool allow_proto);
static AstRef parse_lambda(Parser *p);
static AstRef parse_block_lambda(Parser *p);

/* ---------------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------------- */

static vm_bool is_builtin_type(TokenType ty) {
    return vm_bool_from_int(ty >= TOK_TYPE_I32 && ty <= TOK_TYPE_VOID);
}

/* Lookahead: does `<` at offset k start a generic argument list closed by `>`? */
static vm_bool looks_like_type_args(const Parser *p, vm_u32 k) {
    if (peek_at(p, k) != TOK_LT) return VM_FALSE;
    int depth = 0;
    for (vm_u32 i = k; i < k + 64; i++) {
        TokenType ty = peek_at(p, i);
        if (ty == TOK_LT) depth++;
        else if (ty == TOK_GT) { if (--depth == 0) return VM_TRUE; }
        else if (ty == TOK_SHR) { depth -= 2; if (depth <= 0) return vm_bool_from_int(depth == 0); }
        else if (!(is_name_type(ty) || ty == TOK_COMMA || ty == TOK_LBRACKET || ty == TOK_RBRACKET ||
                   ty == TOK_SELF_TYPE || ty == TOK_DOT)) return VM_FALSE;
    }
    return VM_FALSE;
}

static AstList parse_type_args(Parser *p) {
    vm_u32 mark = p->sp;
    expect(p, TOK_LT, "Expected '<'");
    if (!check(p, TOK_GT)) {
        do { push(p, parse_type(p)); } while (match(p, TOK_COMMA));
    }
    expect(p, TOK_GT, "Expected '>' after type arguments");
    return finish_list(p, mark);
}

static AstRef parse_param(Parser *p);

/* `(` ... `)` in type position: tuple `(A, B)` or function type `(x: A): R` */
static AstRef parse_paren_type(Parser *p) {
    vm_u32 open = expect(p, TOK_LPAREN, "Expected '('");
    vm_u32 mark = p->sp;
    vm_bool all_params = VM_TRUE;
    vm_bool any = VM_FALSE;
    if (!check(p, TOK_RPAREN)) {
        do {
            any = VM_TRUE;
            if (is_name_type(cur(p)) && peek_at(p, 1) == TOK_COLON) {
                push(p, parse_param(p));
            } else {
                all_params = VM_FALSE;
                push(p, parse_type(p));
            }
        } while (match(p, TOK_COMMA));
    }
    expect(p, TOK_RPAREN, "Expected ')' in type");
    AstList items = finish_list(p, mark);
    if (check(p, TOK_COLON) && (all_params || !any)) {
        advance(p);
        AstRef fn = new_node(p, AST_TY_FUNC, open);
        AstRef ret = parse_type(p);
        N(p, fn)->a = items;
        N(p, fn)->b = ret;
        return fn;
    }
    AstRef tup = new_node(p, AST_TY_TUPLE, open);
    N(p, tup)->a = items;
    return tup;
}

static AstRef parse_type(Parser *p) {
    if (!enter(p)) return 0;
    AstRef ty = 0;
    TokenType c = cur(p);
    if (c == TOK_LPAREN) {
        ty = parse_paren_type(p);
    } else if (c == TOK_LBRACE) {
        vm_u32 open = advance(p);
        AstRef k = parse_type(p);
        expect(p, TOK_COLON, "Expected ':' in map type");
        AstRef v = parse_type(p);
        expect(p, TOK_RBRACE, "Expected '}' after map type");
        ty = new_node(p, AST_TY_MAP, open);
        N(p, ty)->a = k;
        N(p, ty)->b = v;
    } else if (is_name_type(c) || c == TOK_SELF_TYPE) {
        vm_u32 name = advance(p);
        ty = new_node(p, AST_TY_NAME, name);
        /* Qualified type names (`mod.Type`) */
        while (check(p, TOK_DOT) && is_name_type(peek_at(p, 1))) {
            advance(p);
            N(p, ty)->tok = advance(p);
        }
        if (is_builtin_type(c) == VM_FALSE && looks_like_type_args(p, 0)) {
            AstList args = parse_type_args(p);
            N(p, ty)->a = args;
        }
        if (match(p, TOK_ELLIPSIS)) N(p, ty)->flags |= AST_F_VARIADIC;
    } else {
        error(p, "Expected type");
        leave(p);
        return 0;
    }
    for (;;) {
        if (check(p, TOK_LBRACKET) && peek_at(p, 1) == TOK_RBRACKET) {
            vm_u32 at = advance(p);
            advance(p);
            AstRef arr = new_node(p, AST_TY_ARRAY, at);
            N(p, arr)->a = ty;
            ty = arr;
        } else if (check(p, TOK_QUESTION)) {
            AstRef opt = new_node(p, AST_TY_OPTIONAL, advance(p));
            N(p, opt)->a = ty;
            ty = opt;
        } else {
            break;
        }
    }
    /* `T ! E`: a value of T, or an error of E */
    if (check(p, TOK_LNOT)) {
        AstRef res = new_node(p, AST_TY_RESULT, advance(p));
        AstRef err = parse_type(p);
        N(p, res)->a = ty;
        N(p, res)->b = err;
        ty = res;
    }
    leave(p);
    return ty;
}

/* ---------------------------------------------------------------------------
 * Attributes & parameters
 * ------------------------------------------------------------------------- */

static AstList parse_args(Parser *p, TokenType close);

static AstList parse_attrs(Parser *p) {
    vm_u32 mark = p->sp;
    while (check(p, TOK_AT_LBRACKET)) {
        advance(p);
        if (!check(p, TOK_RBRACKET)) {
            do {
                vm_u32 name = expect_name(p, "Expected attribute name");
                AstRef at = new_node(p, AST_ATTR, name);
                if (match(p, TOK_LPAREN)) {
                    AstList args = parse_args(p, TOK_RPAREN);
                    N(p, at)->a = args;
                }
                push(p, at);
            } while (match(p, TOK_COMMA));
        }
        expect(p, TOK_RBRACKET, "Expected ']' after attributes");
    }
    return finish_list(p, mark);
}

static AstRef parse_param(Parser *p) {
    vm_u16 flags = 0;
    if (match(p, TOK_ELLIPSIS)) flags |= AST_F_VARIADIC;
    vm_u32 name = expect_name(p, "Expected parameter name");
    AstRef prm = new_node(p, AST_PARAM, name);
    if (match(p, TOK_ELLIPSIS)) flags |= AST_F_VARIADIC;
    if (match(p, TOK_COLON)) {
        AstRef ty = parse_type(p);
        N(p, prm)->a = ty;
        if (ty != 0 && (N(p, ty)->flags & AST_F_VARIADIC)) flags |= AST_F_VARIADIC;
    }
    if (match(p, TOK_ASSIGN)) {
        AstRef def = parse_expr(p);
        N(p, prm)->b = def;
    }
    N(p, prm)->flags = flags;
    return prm;
}

static AstList parse_params(Parser *p) {
    vm_u32 mark = p->sp;
    expect(p, TOK_LPAREN, "Expected '(' before parameters");
    if (!check(p, TOK_RPAREN)) {
        do {
            if (check(p, TOK_RPAREN)) break;
            push(p, parse_param(p));
        } while (match(p, TOK_COMMA));
    }
    expect(p, TOK_RPAREN, "Expected ')' after parameters");
    return finish_list(p, mark);
}

static AstList parse_generic_params(Parser *p) {
    vm_u32 mark = p->sp;
    if (match(p, TOK_LT)) {
        do {
            push(p, expect_name(p, "Expected type parameter name"));
            match(p, TOK_ELLIPSIS);
        } while (match(p, TOK_COMMA));
        expect(p, TOK_GT, "Expected '>' after type parameters");
    }
    return finish_list(p, mark);
}

/* ---------------------------------------------------------------------------
 * Expressions
 * ------------------------------------------------------------------------- */

static AstList parse_args(Parser *p, TokenType close) {
    vm_u32 mark = p->sp;
    vm_bool saved_nb = p->no_brace;
    vm_bool saved_nm = p->no_method;
    p->no_brace = VM_FALSE;
    p->no_method = VM_FALSE;
    if (!check(p, close)) {
        do {
            if (check(p, close)) break;
            push(p, parse_expr(p));
        } while (match(p, TOK_COMMA));
    }
    p->no_brace = saved_nb;
    p->no_method = saved_nm;
    expect(p, close, (close == TOK_RPAREN) ? "Expected ')' after arguments" : "Expected ']' after elements");
    return finish_list(p, mark);
}

/* `(` starts a lambda when it is `() {`, `(): T`, or `(name: T ...` (but not `(obj:method(`) */
static vm_bool is_lambda_paren(const Parser *p) {
    if (peek_at(p, 0) != TOK_LPAREN) return VM_FALSE;
    TokenType t1 = peek_at(p, 1);
    if (t1 == TOK_RPAREN) {
        TokenType t2 = peek_at(p, 2);
        return vm_bool_from_int(t2 == TOK_LBRACE || t2 == TOK_COLON);
    }
    if (is_name_type(t1) && peek_at(p, 2) == TOK_COLON) {
        /* `(x:method(` is a method call on x, `(x: T` is a parameter */
        if (is_name_type(peek_at(p, 3)) && peek_at(p, 4) == TOK_LPAREN) return VM_FALSE;
        return VM_TRUE;
    }
    return VM_FALSE;
}

static AstRef parse_fstring(Parser *p, vm_u32 tok_idx);
static AstRef parse_match(Parser *p, vm_u32 at);

/* Builds a list from every `stride`-th scratch item starting at mark + offset */
static AstList strided_list(Parser *p, vm_u32 mark, vm_u32 count, vm_u32 offset, vm_u32 stride) {
    if (count == 0) return 0;
    AstTree *t = p->t;
    if (grow(t, (void **)&t->extra, &t->extra_cap, t->num_extra + count + 1, sizeof(vm_u32), 1024) == VM_FALSE) return 0;
    AstList l = t->num_extra;
    t->extra[t->num_extra++] = count;
    for (vm_u32 i = 0; i < count; i++) t->extra[t->num_extra++] = p->stack[mark + offset + i * stride];
    return l;
}

static AstRef parse_map_literal(Parser *p) {
    vm_u32 open = advance(p);
    AstRef m = new_node(p, AST_MAP, open);
    if (match(p, TOK_COLON)) {           /* `{:}` empty map */
        expect(p, TOK_RBRACE, "Expected '}' after '{:'");
        return m;
    }
    vm_u32 mark = p->sp;                 /* scratch holds key, value, key, value, ... */
    vm_u32 n = 0;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        vm_bool saved_nm = p->no_method;
        p->no_method = VM_TRUE;
        AstRef k = parse_expr(p);
        p->no_method = saved_nm;
        expect(p, TOK_COLON, "Expected ':' in map literal");
        AstRef v = parse_expr(p);
        push(p, k);
        push(p, v);
        n++;
        if (!match(p, TOK_COMMA)) break;
    }
    expect(p, TOK_RBRACE, "Expected '}' after map literal");
    AstList keys = strided_list(p, mark, n, 0, 2);
    AstList vals = strided_list(p, mark, n, 1, 2);
    p->sp = mark;
    N(p, m)->a = keys;
    N(p, m)->b = vals;
    return m;
}

static AstRef parse_primary(Parser *p) {
    TokenType c = cur(p);
    switch (c) {
        case TOK_INT_LIT:     return new_node(p, AST_INT, advance(p));
        case TOK_FLOAT_LIT:   return new_node(p, AST_FLOAT, advance(p));
        case TOK_STRING_LIT:  return new_node(p, AST_STRING, advance(p));
        case TOK_FSTRING_LIT: return parse_fstring(p, advance(p));
        case TOK_TRUE: case TOK_FALSE: {
            AstRef b = new_node(p, AST_BOOL, advance(p));
            N(p, b)->op = (c == TOK_TRUE) ? 1 : 0;
            return b;
        }
        case TOK_MATCH: {
            vm_u32 at = advance(p);
            AstRef m = parse_match(p, at);
            N(p, m)->flags |= AST_F_MATCH_EXPR;
            return m;
        }
        case TOK_FUNC:
            return parse_lambda(p);
        case TOK_LT:
            return parse_lambda(p);      /* generic lambda `<T>(a: T): T { }` */
        case TOK_LPAREN: {
            if (is_lambda_paren(p)) return parse_lambda(p);
            vm_u32 open = advance(p);
            vm_bool saved_nb = p->no_brace;
            vm_bool saved_nm = p->no_method;
            p->no_brace = VM_FALSE;
            p->no_method = VM_FALSE;
            AstRef first = parse_expr(p);
            if (check(p, TOK_COMMA)) {
                vm_u32 mark = p->sp;
                push(p, first);
                while (match(p, TOK_COMMA)) {
                    if (check(p, TOK_RPAREN)) break;
                    push(p, parse_expr(p));
                }
                expect(p, TOK_RPAREN, "Expected ')' after tuple");
                p->no_brace = saved_nb;
                p->no_method = saved_nm;
                AstRef tup = new_node(p, AST_TUPLE, open);
                AstList items = finish_list(p, mark);
                N(p, tup)->a = items;
                return tup;
            }
            expect(p, TOK_RPAREN, "Expected ')'");
            p->no_brace = saved_nb;
            p->no_method = saved_nm;
            return first;
        }
        case TOK_LBRACKET: {
            vm_u32 open = advance(p);
            AstRef arr = new_node(p, AST_ARRAY, open);
            AstList items = parse_args(p, TOK_RBRACKET);
            N(p, arr)->a = items;
            return arr;
        }
        case TOK_LBRACE:
            if (p->no_brace) break;
            return parse_map_literal(p);
        default:
            if (is_name_type(c) || c == TOK_SELF_TYPE) return new_node(p, AST_IDENT, advance(p));
            break;
    }
    error(p, "Expected expression");
    return 0;
}

/* `{ name = ...` starts a named-argument group (not `==`) */
static vm_bool is_named_group(const Parser *p) {
    return vm_bool_from_int(peek_at(p, 0) == TOK_LBRACE && is_name_type(peek_at(p, 1)) && peek_at(p, 2) == TOK_ASSIGN);
}

static AstRef parse_named_group(Parser *p, AstRef callee) {
    vm_u32 open = advance(p);
    AstRef call = new_node(p, AST_CALL, open);
    vm_u32 mark = p->sp;
    vm_bool saved_nb = p->no_brace, saved_cmd = p->in_command;
    p->no_brace = VM_FALSE;
    p->in_command = VM_FALSE;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        vm_u32 name = expect_name(p, "Expected parameter name in named arguments");
        expect(p, TOK_ASSIGN, "Expected '=' after parameter name");
        AstRef na = new_node(p, AST_NAMED_ARG, name);
        AstRef v = parse_expr(p);
        N(p, na)->a = v;
        push(p, na);
        if (!match(p, TOK_COMMA)) break;
    }
    p->no_brace = saved_nb;
    p->in_command = saved_cmd;
    expect(p, TOK_RBRACE, "Expected '}' after named arguments");
    AstList args = finish_list(p, mark);
    N(p, call)->a = callee;
    N(p, call)->b = args;
    N(p, call)->flags |= AST_F_NAMED_ARGS;
    return call;
}

/* Appends `arg` to a call's argument list (lists are immutable: rebuild) */
static void call_append_arg(Parser *p, AstRef call, AstRef arg) {
    AstList old = N(p, call)->b;
    vm_u32 mark = p->sp;
    for (vm_u32 i = 0; i < ast_list_len(p->t, old); i++) push(p, ast_list_at(p->t, old, i));
    push(p, arg);
    AstList nl = finish_list(p, mark);
    N(p, call)->b = nl;
}

/* Trailing block `callee { ... }` / `call(...) { ... }`: adds a closure argument */
static AstRef attach_trailing_block(Parser *p, AstRef e) {
    if (is_named_group(p)) return parse_named_group(p, e);
    AstRef blk = parse_block_lambda(p);
    if (N(p, e)->kind == AST_CALL && !(N(p, e)->flags & AST_F_NAMED_ARGS)) {
        call_append_arg(p, e, blk);
        return e;
    }
    AstRef call = new_node(p, AST_CALL, N(p, blk)->tok);
    vm_u32 mark = p->sp;
    push(p, blk);
    AstList args = finish_list(p, mark);
    N(p, call)->a = e;
    N(p, call)->b = args;
    return call;
}

static vm_bool trailing_block_allowed(const Parser *p, AstRef e) {
    if (p->no_brace || p->in_command || !check(p, TOK_LBRACE) || !same_line(p)) return VM_FALSE;
    vm_u8 k = p->t->nodes[e].kind;
    return vm_bool_from_int(k == AST_IDENT || k == AST_FIELD_GET || k == AST_CALL || k == AST_METHOD);
}

static AstRef parse_postfix(Parser *p, AstRef e) {
    for (;;) {
        TokenType c = cur(p);
        if (c == TOK_LPAREN && p->in_command && !adjacent(p)) break;
        if (trailing_block_allowed(p, e)) {
            e = attach_trailing_block(p, e);
            continue;
        }
        if (c == TOK_LPAREN) {
            vm_u32 at = advance(p);
            AstRef call = new_node(p, AST_CALL, at);
            AstList args = parse_args(p, TOK_RPAREN);
            N(p, call)->a = e;
            N(p, call)->b = args;
            e = call;
        } else if (c == TOK_QUESTION && same_line(p)) {
            /* `expr?`: unwrap a `T ! E` result or return its error */
            AstRef pr = new_node(p, AST_PROPAGATE, advance(p));
            N(p, pr)->a = e;
            e = pr;
        } else if (c == TOK_QDOT && is_name_type(peek_at(p, 1))) {
            advance(p);
            AstRef oc = new_node(p, AST_OPT_CHAIN, advance(p));
            N(p, oc)->a = e;
            if (check(p, TOK_LPAREN) && adjacent(p)) {
                advance(p);
                AstList args = parse_args(p, TOK_RPAREN);
                N(p, oc)->b = args;
                N(p, oc)->flags |= AST_F_CALL_ARGS;
            }
            e = oc;
        } else if (c == TOK_DOT && is_name_type(peek_at(p, 1))) {
            advance(p);
            AstRef fg = new_node(p, AST_FIELD_GET, advance(p));
            N(p, fg)->a = e;
            e = fg;
        } else if (c == TOK_COLON && !p->no_method && is_name_type(peek_at(p, 1)) && peek_at(p, 2) == TOK_LPAREN) {
            advance(p);
            AstRef mc = new_node(p, AST_METHOD, advance(p));
            advance(p); /* ( */
            AstList args = parse_args(p, TOK_RPAREN);
            N(p, mc)->a = e;
            N(p, mc)->b = args;
            e = mc;
        } else if (c == TOK_LBRACKET && tok_at(p, p->pos)->line == tok_at(p, p->pos - 1)->line) {
            vm_u32 at = advance(p);
            vm_bool saved_nb = p->no_brace;
            p->no_brace = VM_FALSE;
            AstRef idx = parse_expr(p);
            p->no_brace = saved_nb;
            expect(p, TOK_RBRACKET, "Expected ']' after index");
            AstRef ix = new_node(p, AST_INDEX, at);
            N(p, ix)->a = e;
            N(p, ix)->b = idx;
            e = ix;
        } else if (c == TOK_LT && (N(p, e)->kind == AST_IDENT || N(p, e)->kind == AST_FIELD_GET) && looks_like_type_args(p, 0)) {
            /* generic call `name<T>(...)`: only when `>` is directly followed by `(` */
            vm_u32 save = p->pos;
            vm_u32 save_sp = p->sp;
            int depth = 0;
            vm_u32 k = 0;
            for (;; k++) {
                TokenType ty = peek_at(p, k);
                if (ty == TOK_LT) depth++;
                else if (ty == TOK_GT && --depth == 0) break;
                else if (ty == TOK_EOF) break;
            }
            if (peek_at(p, k + 1) != TOK_LPAREN) break;
            AstList targs = parse_type_args(p);
            if (!check(p, TOK_LPAREN)) {
                p->pos = save;
                p->sp = save_sp;
                break;
            }
            vm_u32 at = advance(p);
            AstRef call = new_node(p, AST_CALL, at);
            AstList args = parse_args(p, TOK_RPAREN);
            N(p, call)->a = e;
            N(p, call)->b = args;
            N(p, call)->c = targs;
            e = call;
        } else {
            break;
        }
    }
    return e;
}

static AstRef parse_unary(Parser *p) {
    TokenType c = cur(p);
    if (c == TOK_MINUS || c == TOK_LNOT || c == TOK_BNOT || c == TOK_HASH) {
        if (!enter(p)) return 0;
        vm_u32 at = advance(p);
        AstRef operand = parse_unary(p);
        AstRef u = new_node(p, AST_UNARY, at);
        N(p, u)->op = (vm_u8)c;
        N(p, u)->a = operand;
        leave(p);
        return u;
    }
    return parse_postfix(p, parse_primary(p));
}

static int binary_prec(TokenType ty) {
    switch (ty) {
        case TOK_QQ:                                    return 1;
        case TOK_LOR:                                   return 2;
        case TOK_LAND:                                  return 3;
        case TOK_EQ: case TOK_NE: case TOK_LT: case TOK_LE:
        case TOK_GT: case TOK_GE: case TOK_IN: case TOK_IS: return 4;
        case TOK_DOTDOT:                                return 5;
        case TOK_BOR:                                   return 6;
        case TOK_BXOR:                                  return 7;
        case TOK_BAND:                                  return 8;
        case TOK_SHL: case TOK_SHR:                     return 9;
        case TOK_PLUS: case TOK_MINUS:                  return 10;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return 11;
        case TOK_AS:                                    return 12;
        default:                                        return 0;
    }
}

static AstRef parse_binary(Parser *p, int min_prec) {
    if (!enter(p)) return 0;
    AstRef lhs = parse_unary(p);
    for (;;) {
        TokenType op = cur(p);
        int prec = binary_prec(op);
        if (prec == 0 || prec < min_prec) break;
        vm_u32 at = advance(p);
        AstRef rhs;
        if (op == TOK_AS) {
            rhs = parse_type(p);
        } else if (op == TOK_IS) {
            rhs = check(p, TOK_STRING_LIT) ? new_node(p, AST_STRING, advance(p)) : parse_type(p);
        } else {
            rhs = parse_binary(p, prec + 1);
        }
        AstRef bin = new_node(p, (op == TOK_DOTDOT) ? AST_RANGE : AST_BINARY, at);
        N(p, bin)->op = (vm_u8)op;
        N(p, bin)->a = lhs;
        N(p, bin)->b = rhs;
        lhs = bin;
    }
    leave(p);
    return lhs;
}

/* expr := [try] binary [or (expr | [name] block)] */
static AstRef parse_expr(Parser *p) {
    if (!enter(p)) return 0;
    vm_bool has_try = VM_FALSE;
    vm_u32 start = p->pos;
    if (cur_is_word(p, "try") && binary_prec(peek_at(p, 1)) == 0 && peek_at(p, 1) != TOK_ASSIGN &&
        peek_at(p, 1) != TOK_LPAREN && peek_at(p, 1) != TOK_DOT && peek_at(p, 1) != TOK_RPAREN &&
        peek_at(p, 1) != TOK_COMMA) {
        advance(p);
        has_try = VM_TRUE;
    }
    AstRef e = parse_binary(p, 1);
    if (check(p, TOK_OR) || has_try) {
        AstRef tr = new_node(p, AST_TRY, 0);
        N(p, tr)->a = e;
        if (has_try) N(p, tr)->flags |= AST_F_TRY_KW;
        if (match(p, TOK_OR)) {
            AstRef fb;
            if (check(p, TOK_LBRACE)) {
                fb = parse_block(p);
            } else if (cur(p) == TOK_IDENT && peek_at(p, 1) == TOK_LBRACE) {
                N(p, tr)->tok = advance(p);
                fb = parse_block(p);
            } else {
                fb = parse_binary(p, 1);
            }
            N(p, tr)->b = fb;
        }
        (void)start;
        e = tr;
    }
    while (cur_is_word(p, "catch")) {
        AstRef ct = new_node(p, AST_CATCH, advance(p));
        N(p, ct)->a = e;
        AstRef fb;
        if (check(p, TOK_LBRACE)) {
            fb = parse_block(p);
        } else if (cur(p) == TOK_IDENT && peek_at(p, 1) == TOK_LBRACE) {
            N(p, ct)->tok = advance(p);
            fb = parse_block(p);
        } else {
            fb = parse_binary(p, 1);
        }
        N(p, ct)->b = fb;
        e = ct;
    }
    leave(p);
    return e;
}

/* Condition / head expression: a following `{` belongs to the statement, not a map literal */
static AstRef parse_head_expr(Parser *p) {
    vm_bool saved = p->no_brace;
    p->no_brace = VM_TRUE;
    AstRef e = parse_expr(p);
    p->no_brace = saved;
    return e;
}

static AstRef parse_lambda(Parser *p) {
    vm_u32 at = p->pos;
    match(p, TOK_FUNC);
    AstRef fn = new_node(p, AST_FUNC, at);
    vm_u32 ex = alloc_extra(p, 3);
    N(p, fn)->d = ex;
    N(p, fn)->flags = AST_F_LAMBDA;
    if (is_name_type(cur(p)) && peek_at(p, 1) != TOK_COLON) {
        N(p, fn)->tok = advance(p);     /* named function expression */
        N(p, fn)->flags |= AST_F_NAMED;
    }
    AstList generics = parse_generic_params(p);
    p->t->extra[ex + 1] = generics;
    AstList params = parse_params(p);
    N(p, fn)->a = params;
    if (match(p, TOK_COLON)) {
        AstRef ret = parse_type(p);
        N(p, fn)->c = ret;
    }
    vm_bool saved_nb = p->no_brace;
    p->no_brace = VM_FALSE;
    AstRef body = parse_block(p);
    p->no_brace = saved_nb;
    N(p, fn)->b = body;
    return fn;
}

/* `{ a, b -> stmts }` / `{ stmts }`: closure whose parameter types come from the callee */
static AstRef parse_block_lambda(Parser *p) {
    vm_u32 open = p->pos;
    AstRef fn = new_node(p, AST_FUNC, open);
    vm_u32 ex = alloc_extra(p, 3);
    N(p, fn)->d = ex;
    N(p, fn)->flags = AST_F_LAMBDA | AST_F_BLOCK;

    /* Look ahead for `name (, name)* ->` right after `{` */
    vm_u32 k = 1;
    vm_bool has_params = VM_FALSE;
    if (peek_at(p, 1) == TOK_ARROW) {
        has_params = VM_TRUE;
    } else {
        while (is_name_type(peek_at(p, k)) && (peek_at(p, k + 1) == TOK_COMMA || peek_at(p, k + 1) == TOK_ARROW)) {
            if (peek_at(p, k + 1) == TOK_ARROW) { has_params = VM_TRUE; break; }
            k += 2;
        }
    }

    AstRef blk = new_node(p, AST_BLOCK, open);
    advance(p); /* { */
    vm_u32 pmark = p->sp;
    if (has_params) {
        while (!check(p, TOK_ARROW) && !check(p, TOK_EOF)) {
            vm_u32 name = expect_name(p, "Expected block parameter name");
            push(p, new_node(p, AST_PARAM, name));
            if (!match(p, TOK_COMMA)) break;
        }
        expect(p, TOK_ARROW, "Expected '->' after block parameters");
    }
    AstList params = finish_list(p, pmark);
    N(p, fn)->a = params;

    vm_bool saved_nb = p->no_brace, saved_nm = p->no_method, saved_cmd = p->in_command;
    p->no_brace = VM_FALSE;
    p->no_method = VM_FALSE;
    p->in_command = VM_FALSE;
    vm_u32 mark = p->sp;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (match(p, TOK_SEMICOLON)) continue;
        AstRef st = parse_stmt(p);
        if (st != 0) push(p, st);
    }
    expect(p, TOK_RBRACE, "Expected '}' to close block");
    p->no_brace = saved_nb;
    p->no_method = saved_nm;
    p->in_command = saved_cmd;
    AstList stmts = finish_list(p, mark);
    N(p, blk)->a = stmts;
    N(p, fn)->b = blk;
    return fn;
}

/* f"text {expr} {{literal}}": text runs become AST_STR_PART, `{...}` is parsed as an expression */
static AstRef parse_fstring(Parser *p, vm_u32 tok_idx) {
    AstRef fs = new_node(p, AST_FSTRING, tok_idx);
    const char *s = tok_at(p, tok_idx)->start;
    vm_u32 len = tok_at(p, tok_idx)->length;
    vm_u32 line = tok_at(p, tok_idx)->line;
    vm_u32 mark = p->sp;
    vm_u32 run = 0;
    vm_u32 i = 0;
    while (i < len) {
        if (s[i] == '{' && i + 1 < len && s[i + 1] == '{') { i += 2; continue; }
        if (s[i] == '}' && i + 1 < len && s[i + 1] == '}') { i += 2; continue; }
        if (s[i] != '{') { i++; continue; }
        if (i > run) {
            AstRef part = new_node(p, AST_STR_PART, tok_idx);
            N(p, part)->a = run;
            N(p, part)->b = i - run;
            push(p, part);
        }
        vm_u32 e_start = i + 1;
        vm_u32 j = e_start;
        int depth = 1;
        while (j < len) {
            char ch = s[j];
            if (ch == '"' || ch == '\'') {
                char q = ch;
                j++;
                while (j < len && s[j] != q) { if (s[j] == '\\') j++; j++; }
            } else if (ch == '{') {
                depth++;
            } else if (ch == '}') {
                if (--depth == 0) break;
            }
            j++;
        }
        if (j >= len) {
            error_at(p, tok_idx, "Unclosed '{' in f-string");
            p->sp = mark;
            return fs;
        }
        /* Copy the expression text so it can be lexed on its own (NUL-terminated) */
        AstTree *t = p->t;
        vm_u32 elen = j - e_start;
        char *buf = (char *)t->realloc_fn(t->alloc_ud, VM_NULL, 0, elen + 1);
        if (buf == VM_NULL || grow(t, (void **)&t->bufs, &t->bufs_cap, t->num_bufs + 1, sizeof(char *), 16) == VM_FALSE) {
            t->out_of_memory = VM_TRUE;
            p->sp = mark;
            return fs;
        }
        for (vm_u32 k = 0; k < elen; k++) buf[k] = s[e_start + k];
        buf[elen] = '\0';
        t->bufs[t->num_bufs++] = buf;

        Parser sub = *p;
        sub.pos = lex_into(t, buf, line);
        sub.end = t->num_toks - 1;
        sub.no_brace = VM_FALSE;
        sub.no_method = VM_FALSE;
        AstRef ex = parse_expr(&sub);
        if (!sub.panic && sub.pos != sub.end) error_at(&sub, sub.pos, "Unexpected token in f-string expression");
        p->stack = sub.stack;
        p->stack_cap = sub.stack_cap;
        if (sub.panic) {
            p->panic = VM_TRUE;
            p->sp = mark;
            return fs;
        }
        push(p, ex);
        i = j + 1;
        run = i;
    }
    if (len > run) {
        AstRef part = new_node(p, AST_STR_PART, tok_idx);
        N(p, part)->a = run;
        N(p, part)->b = len - run;
        push(p, part);
    }
    AstList parts = finish_list(p, mark);
    N(p, fs)->a = parts;
    return fs;
}

/* ---------------------------------------------------------------------------
 * Statements
 * ------------------------------------------------------------------------- */

static AstRef parse_block(Parser *p) {
    if (!enter(p)) return 0;
    vm_u32 open = expect(p, TOK_LBRACE, "Expected '{'");
    AstRef blk = new_node(p, AST_BLOCK, open);
    vm_bool saved_nb = p->no_brace;
    vm_bool saved_nm = p->no_method;
    p->no_brace = VM_FALSE;
    p->no_method = VM_FALSE;
    vm_u32 mark = p->sp;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (match(p, TOK_SEMICOLON)) continue;
        AstRef st = parse_stmt(p);
        if (st != 0) push(p, st);
    }
    expect(p, TOK_RBRACE, "Expected '}' to close block");
    p->no_brace = saved_nb;
    p->no_method = saved_nm;
    AstList stmts = finish_list(p, mark);
    N(p, blk)->a = stmts;
    leave(p);
    return blk;
}

static AstRef parse_let(Parser *p, vm_bool is_const, vm_u32 at) {
    AstRef let = new_node(p, AST_LET, at);
    if (is_const) N(p, let)->flags |= AST_F_CONST;
    vm_u32 mark = p->sp;
    do { push(p, expect_name(p, "Expected variable name")); } while (match(p, TOK_COMMA));
    AstList names = finish_list(p, mark);
    N(p, let)->a = names;
    if (match(p, TOK_COLON)) {
        AstRef ty = parse_type(p);
        N(p, let)->b = ty;
    }
    if (match(p, TOK_ASSIGN)) {
        AstRef init = parse_expr(p);
        if (check(p, TOK_COMMA)) {
            vm_u32 vmark = p->sp;
            push(p, init);
            while (match(p, TOK_COMMA)) push(p, parse_expr(p));
            AstRef tup = new_node(p, AST_TUPLE, N(p, let)->tok);
            AstList items = finish_list(p, vmark);
            N(p, tup)->a = items;
            init = tup;
        }
        N(p, let)->c = init;
    }
    return let;
}

/* `if` / `while` head: [const] [let name = expr ;] cond */
static void parse_cond_head(Parser *p, AstRef node) {
    if (match(p, TOK_CONST)) N(p, node)->flags |= AST_F_CONST;
    if (check(p, TOK_LET)) {
        vm_u32 at = advance(p);
        vm_bool saved = p->no_brace;
        p->no_brace = VM_TRUE;
        AstRef let = parse_let(p, VM_FALSE, at);
        p->no_brace = saved;
        N(p, node)->d = let;
        N(p, node)->flags |= AST_F_LET;
        /* `if let x = opt { }`: no condition means "the optional is present" */
        if (!match(p, TOK_SEMICOLON)) return;
    }
    AstRef cond = parse_head_expr(p);
    N(p, node)->a = cond;
}

static AstRef parse_if(Parser *p, vm_u32 at) {
    if (!enter(p)) return 0;
    AstRef node = new_node(p, AST_IF, at);
    parse_cond_head(p, node);
    AstRef then = parse_block(p);
    N(p, node)->b = then;
    if (check(p, TOK_ELSE)) {
        advance(p);
        AstRef els;
        if (check(p, TOK_IF)) {
            vm_u32 at2 = advance(p);
            els = parse_if(p, at2);
        } else {
            els = parse_block(p);
        }
        N(p, node)->c = els;
    }
    leave(p);
    return node;
}

static AstRef parse_match(Parser *p, vm_u32 at) {
    AstRef node = new_node(p, AST_MATCH, at);
    AstRef subject = parse_head_expr(p);
    N(p, node)->a = subject;
    expect(p, TOK_LBRACE, "Expected '{' after match subject");
    vm_u32 mark = p->sp;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (match(p, TOK_COMMA) || match(p, TOK_SEMICOLON)) continue;
        vm_u32 arm_at = p->pos;
        AstRef pat = 0;
        if (cur_is_word(p, "_") && peek_at(p, 1) == TOK_FAT_ARROW) {
            advance(p);
        } else {
            pat = parse_head_expr(p);
        }
        expect(p, TOK_FAT_ARROW, "Expected '=>' in match arm");
        AstRef body = check(p, TOK_LBRACE) ? parse_block(p) : parse_expr(p);
        AstRef arm = new_node(p, AST_ARM, arm_at);
        N(p, arm)->a = pat;
        N(p, arm)->b = body;
        push(p, arm);
    }
    expect(p, TOK_RBRACE, "Expected '}' after match arms");
    AstList arms = finish_list(p, mark);
    N(p, node)->b = arms;
    return node;
}

static vm_bool is_assign_op(TokenType ty) {
    return vm_bool_from_int(ty == TOK_ASSIGN || (ty >= TOK_PLUS_EQ && ty <= TOK_SHR_EQ));
}

/* `defer` is a contextual keyword: `defer { ... }` or `defer <statement>` on the same line */
static vm_bool is_defer_start(const Parser *p) {
    if (!cur_is_word(p, "defer")) return VM_FALSE;
    TokenType nx = peek_at(p, 1);
    if (nx == TOK_LBRACE) return VM_TRUE;
    if (tok_at(p, p->pos + 1)->line != tok_at(p, p->pos)->line) return VM_FALSE;
    if (is_assign_op(nx) || nx == TOK_LPAREN || nx == TOK_DOT || nx == TOK_COLON ||
        nx == TOK_LBRACKET || nx == TOK_COMMA || binary_prec(nx) != 0) return VM_FALSE;
    return VM_TRUE;
}

/* `throw` is a contextual keyword: `throw <expr>` on the same line */
static vm_bool is_throw_start(const Parser *p) {
    if (!cur_is_word(p, "throw")) return VM_FALSE;
    TokenType nx = peek_at(p, 1);
    if (tok_at(p, p->pos + 1)->line != tok_at(p, p->pos)->line) return VM_FALSE;
    if (is_assign_op(nx) || nx == TOK_LPAREN || nx == TOK_DOT || nx == TOK_COLON || nx == TOK_LBRACE ||
        nx == TOK_LBRACKET || nx == TOK_COMMA || nx == TOK_RBRACE || nx == TOK_SEMICOLON ||
        binary_prec(nx) != 0) return VM_FALSE;
    return VM_TRUE;
}

/* Tokens that can start a command-call argument (`f "a", 2`, `f x`) */
static vm_bool is_command_arg_start(TokenType t) {
    switch (t) {
        case TOK_STRING_LIT: case TOK_FSTRING_LIT: case TOK_INT_LIT: case TOK_FLOAT_LIT:
        case TOK_TRUE: case TOK_FALSE: case TOK_IDENT: case TOK_SELF_TYPE:
            return VM_TRUE;
        default:
            return VM_FALSE;
    }
}

/* Statement `name[.name]* arg ...` with the first argument on the same line */
static vm_bool is_command_call_start(const Parser *p) {
    if (cur(p) != TOK_IDENT) return VM_FALSE;
    vm_u32 k = 0;
    while (peek_at(p, k + 1) == TOK_DOT && is_name_type(peek_at(p, k + 2))) k += 2;
    if (is_command_arg_start(peek_at(p, k + 1)) == VM_FALSE) return VM_FALSE;
    vm_u32 a = p->pos + k, b = p->pos + k + 1;
    if (b > p->end) return VM_FALSE;
    return vm_bool_from_int(p->t->toks[a].line == p->t->toks[b].line);
}

/* `f a, b [{ block }] [(more)]...`: arguments end at the line end or at a spaced `(` / `{` */
static AstRef parse_command_call(Parser *p) {
    vm_u32 at = p->pos;
    AstRef callee = new_node(p, AST_IDENT, advance(p));
    while (check(p, TOK_DOT) && is_name_type(peek_at(p, 1))) {
        advance(p);
        AstRef fg = new_node(p, AST_FIELD_GET, advance(p));
        N(p, fg)->a = callee;
        callee = fg;
    }
    vm_bool saved_nb = p->no_brace, saved_cmd = p->in_command;
    p->no_brace = VM_TRUE;
    p->in_command = VM_TRUE;
    vm_u32 mark = p->sp;
    do {
        push(p, parse_expr(p));
    } while (check(p, TOK_COMMA) && same_line(p) && match(p, TOK_COMMA));
    p->no_brace = saved_nb;
    p->in_command = saved_cmd;
    AstList args = finish_list(p, mark);
    AstRef call = new_node(p, AST_CALL, at);
    N(p, call)->a = callee;
    N(p, call)->b = args;
    N(p, call)->flags |= AST_F_COMMAND;

    /* Following groups on the same line: `{ block }` adds a closure arg, `( ... )` calls the result */
    for (;;) {
        if (check(p, TOK_LBRACE) && same_line(p)) {
            call = attach_trailing_block(p, call);
        } else if (check(p, TOK_LPAREN) && same_line(p)) {
            vm_u32 lp = advance(p);
            AstRef next = new_node(p, AST_CALL, lp);
            AstList nargs = parse_args(p, TOK_RPAREN);
            N(p, next)->a = call;
            N(p, next)->b = nargs;
            call = next;
        } else {
            break;
        }
    }
    return call;
}

static AstRef parse_stmt(Parser *p) {
    if (!enter(p)) return 0;
    AstRef st = 0;
    vm_u32 at = p->pos;
    TokenType c = cur(p);

    if (c == TOK_FUNC && is_name_type(peek_at(p, 1))) {
        advance(p);
        st = parse_func(p, AST_F_NESTED, 0, VM_FALSE);
    } else if (c == TOK_AT_LBRACKET) {
        AstList attrs = parse_attrs(p);
        expect(p, TOK_FUNC, "Expected 'func' after attributes");
        st = parse_func(p, AST_F_NESTED, attrs, VM_FALSE);
    } else if (c == TOK_LET || c == TOK_CONST) {
        advance(p);
        st = parse_let(p, vm_bool_from_int(c == TOK_CONST), at);
    } else if (c == TOK_IF) {
        advance(p);
        st = parse_if(p, at);
    } else if (c == TOK_MATCH) {
        advance(p);
        st = parse_match(p, at);
    } else if (c == TOK_WHILE) {
        advance(p);
        st = new_node(p, AST_WHILE, at);
        parse_cond_head(p, st);
        AstRef body = parse_block(p);
        N(p, st)->b = body;
    } else if (c == TOK_FOR) {
        advance(p);
        st = new_node(p, AST_FOR, at);
        vm_u32 mark = p->sp;
        do { push(p, expect_name(p, "Expected loop variable")); } while (match(p, TOK_COMMA));
        AstList vars = finish_list(p, mark);
        N(p, st)->a = vars;
        expect(p, TOK_IN, "Expected 'in' in for loop");
        AstRef iter = parse_head_expr(p);
        N(p, st)->b = iter;
        AstRef body = parse_block(p);
        N(p, st)->c = body;
    } else if (c == TOK_BREAK) {
        advance(p);
        st = new_node(p, AST_BREAK, at);
    } else if (c == TOK_CONTINUE) {
        advance(p);
        st = new_node(p, AST_CONTINUE, at);
    } else if (c == TOK_PRINT && peek_at(p, 1) != TOK_LPAREN && is_command_arg_start(peek_at(p, 1)) &&
               tok_at(p, p->pos + 1)->line == tok_at(p, p->pos)->line) {
        /* `print value` */
        advance(p);
        st = new_node(p, AST_PRINT, at);
        AstRef e = parse_expr(p);
        N(p, st)->a = e;
    } else if (c == TOK_PRINT && peek_at(p, 1) == TOK_LPAREN) {
        advance(p);
        advance(p);
        st = new_node(p, AST_PRINT, at);
        AstRef e = parse_expr(p);
        N(p, st)->a = e;
        expect(p, TOK_RPAREN, "Expected ')' after print argument");
    } else if (c == TOK_RETURN) {
        advance(p);
        st = new_node(p, AST_RETURN, at);
        TokenType nx = cur(p);
        vm_bool bare = vm_bool_from_int(nx == TOK_RBRACE || nx == TOK_SEMICOLON || nx == TOK_EOF ||
                                        tok_at(p, p->pos)->line != tok_at(p, at)->line);
        if (!bare) {
            vm_u32 mark = p->sp;
            do { push(p, parse_expr(p)); } while (match(p, TOK_COMMA));
            AstList vals = finish_list(p, mark);
            N(p, st)->a = vals;
        }
    } else if (is_defer_start(p)) {
        advance(p);
        st = new_node(p, AST_DEFER, at);
        AstRef body = check(p, TOK_LBRACE) ? parse_block(p) : parse_stmt(p);
        N(p, st)->a = body;
    } else if (is_throw_start(p)) {
        advance(p);
        st = new_node(p, AST_THROW, at);
        AstRef e = parse_expr(p);
        N(p, st)->a = e;
    } else if (c == TOK_FAT_ARROW) {
        advance(p);
        st = new_node(p, AST_YIELD, at);
        AstRef e = parse_expr(p);
        N(p, st)->a = e;
    } else if (c == TOK_LBRACE) {
        st = parse_block(p);
    } else if (is_command_call_start(p)) {
        AstRef call = parse_command_call(p);
        st = new_node(p, AST_EXPR_STMT, at);
        N(p, st)->a = call;
    } else {
        /* expression statement, or assignment `a, b.c, d[i] op= values` */
        vm_u32 mark = p->sp;
        push(p, parse_expr(p));
        while (match(p, TOK_COMMA)) push(p, parse_expr(p));
        if (is_assign_op(cur(p))) {
            TokenType op = cur(p);
            vm_u32 op_at = advance(p);
            AstList targets = finish_list(p, mark);
            vm_u32 vmark = p->sp;
            do { push(p, parse_expr(p)); } while (match(p, TOK_COMMA));
            AstList vals = finish_list(p, vmark);
            st = new_node(p, AST_ASSIGN, op_at);
            N(p, st)->op = (vm_u8)op;
            N(p, st)->a = targets;
            N(p, st)->b = vals;
        } else if (p->sp - mark == 1) {
            AstRef e = p->stack[mark];
            p->sp = mark;
            st = new_node(p, AST_EXPR_STMT, at);
            N(p, st)->a = e;
        } else {
            p->sp = mark;
            error(p, "Expected '=' after comma-separated targets");
        }
    }
    match(p, TOK_SEMICOLON);
    leave(p);
    return st;
}

/* ---------------------------------------------------------------------------
 * Declarations
 * ------------------------------------------------------------------------- */

/* After `func`: [Owner.]*name [<T>] (params) [: ret] [body] */
static AstRef parse_func(Parser *p, vm_u16 flags, AstList attrs, vm_bool allow_proto) {
    vm_u32 at = p->pos;
    AstRef fn = new_node(p, AST_FUNC, at);
    vm_u32 ex = alloc_extra(p, 3);
    N(p, fn)->d = ex;
    N(p, fn)->flags = flags;
    p->t->extra[ex + 2] = attrs;

    vm_u32 mark = p->sp;
    vm_u32 name = expect_name(p, "Expected function name");
    while (check(p, TOK_DOT) && is_name_type(peek_at(p, 1))) {
        push(p, name);
        advance(p);
        name = advance(p);
    }
    AstList owner = finish_list(p, mark);
    p->t->extra[ex + 0] = owner;
    N(p, fn)->tok = name;

    AstList generics = parse_generic_params(p);
    p->t->extra[ex + 1] = generics;
    AstList params = parse_params(p);
    N(p, fn)->a = params;
    if (match(p, TOK_COLON)) {
        AstRef ret = parse_type(p);
        N(p, fn)->c = ret;
    }
    if (check(p, TOK_LBRACE)) {
        AstRef body = parse_block(p);
        N(p, fn)->b = body;
    } else if (allow_proto) {
        N(p, fn)->flags |= AST_F_PROTO;
    } else {
        error(p, "Expected '{' to start function body");
    }
    return fn;
}

/* After `operator`: [Owner.]op (params) [: ret] [body] */
static AstRef parse_operator(Parser *p, vm_u16 flags, AstList attrs, vm_bool allow_proto) {
    AstRef fn = new_node(p, AST_FUNC, p->pos);
    vm_u32 ex = alloc_extra(p, 3);
    N(p, fn)->d = ex;
    N(p, fn)->flags = (vm_u16)(flags | AST_F_OPERATOR);
    p->t->extra[ex + 2] = attrs;

    vm_u32 mark = p->sp;
    while (cur(p) == TOK_IDENT && peek_at(p, 1) == TOK_DOT) {
        push(p, advance(p));
        advance(p);
    }
    AstList owner = finish_list(p, mark);
    p->t->extra[ex + 0] = owner;

    vm_u32 op_start = p->pos;
    vm_u8 op_len = 1;
    TokenType c = cur(p);
    if (c == TOK_LPAREN && peek_at(p, 1) == TOK_RPAREN) {
        op_len = 2;                                      /* () */
    } else if (c == TOK_LBRACKET && peek_at(p, 1) == TOK_RBRACKET) {
        op_len = (peek_at(p, 2) == TOK_ASSIGN) ? 3 : 2;  /* [] or []= */
    } else if (c == TOK_EOF || c == TOK_LBRACE) {
        error(p, "Expected operator name");
        return fn;
    }
    for (vm_u8 i = 0; i < op_len; i++) advance(p);
    N(p, fn)->tok = op_start;
    N(p, fn)->op = op_len;

    AstList params = parse_params(p);
    N(p, fn)->a = params;
    if (match(p, TOK_COLON)) {
        AstRef ret = parse_type(p);
        N(p, fn)->c = ret;
    }
    if (check(p, TOK_LBRACE)) {
        AstRef body = parse_block(p);
        N(p, fn)->b = body;
    } else if (allow_proto) {
        N(p, fn)->flags |= AST_F_PROTO;
    } else {
        error(p, "Expected '{' to start operator body");
    }
    return fn;
}

/* Struct / type-header / contract members */
static AstList parse_members(Parser *p, vm_bool allow_proto) {
    vm_u32 mark = p->sp;
    expect(p, TOK_LBRACE, "Expected '{'");
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (match(p, TOK_COMMA) || match(p, TOK_SEMICOLON)) continue;
        AstList attrs = parse_attrs(p);
        vm_u16 flags = 0;
        if (match(p, TOK_PUB)) flags |= AST_F_PUB;
        if (match(p, TOK_FUNC)) {
            push(p, parse_func(p, flags, attrs, allow_proto));
        } else if (match(p, TOK_OPERATOR)) {
            push(p, parse_operator(p, flags, attrs, allow_proto));
        } else {
            vm_u32 name = expect_name(p, "Expected field, 'func' or 'operator'");
            AstRef fld = new_node(p, AST_FIELD, name);
            N(p, fld)->flags = flags;
            if (match(p, TOK_COLON)) {
                AstRef ty = parse_type(p);
                N(p, fld)->a = ty;
            }
            if (match(p, TOK_ASSIGN)) {
                AstRef def = parse_expr(p);
                N(p, fld)->b = def;
            }
            push(p, fld);
        }
    }
    expect(p, TOK_RBRACE, "Expected '}'");
    return finish_list(p, mark);
}

static AstRef parse_struct(Parser *p, vm_u16 flags, AstList attrs) {
    vm_u32 name = expect_name(p, "Expected struct name");
    AstRef st = new_node(p, AST_STRUCT, name);
    N(p, st)->flags = flags;
    N(p, st)->d = attrs;
    AstList generics = parse_generic_params(p);
    N(p, st)->a = generics;
    if (match(p, TOK_COLON)) {
        vm_u32 mark = p->sp;
        do { push(p, parse_type(p)); } while (match(p, TOK_COMMA));
        AstList parents = finish_list(p, mark);
        N(p, st)->b = parents;
    }
    AstList members = parse_members(p, vm_bool_from_int((flags & AST_F_TYPE_HDR) != 0));
    N(p, st)->c = members;
    return st;
}

static AstRef parse_type_decl(Parser *p, vm_u16 flags, AstList attrs) {
    if (match(p, TOK_FUNC)) return parse_func(p, (vm_u16)(flags | AST_F_PROTO), attrs, VM_TRUE);
    if (match(p, TOK_STRUCT)) return parse_struct(p, (vm_u16)(flags | AST_F_TYPE_HDR), attrs);
    vm_u32 name = expect_name(p, "Expected type name");
    if (match(p, TOK_ASSIGN)) {
        AstRef al = new_node(p, AST_TYPE_ALIAS, name);
        N(p, al)->flags = flags;
        AstRef ty = parse_type(p);
        N(p, al)->a = ty;
        return al;
    }
    AstRef ct = new_node(p, AST_CONTRACT, name);
    N(p, ct)->flags = flags;
    AstList generics = parse_generic_params(p);
    N(p, ct)->a = generics;
    AstList members = parse_members(p, VM_TRUE);
    N(p, ct)->c = members;
    return ct;
}

/* `as a.b.c` -> token of the last segment */
static vm_u32 parse_ns_alias(Parser *p) {
    vm_u32 last = expect_name(p, "Expected namespace after 'as'");
    while (match(p, TOK_DOT)) last = expect_name(p, "Expected identifier after '.'");
    return last;
}

static AstRef parse_enum(Parser *p, vm_u16 flags) {
    vm_u32 name = expect_name(p, "Expected enum name");
    AstRef en = new_node(p, AST_ENUM, name);
    N(p, en)->flags = flags;
    if (match(p, TOK_AS)) N(p, en)->b = parse_ns_alias(p);
    expect(p, TOK_LBRACE, "Expected '{' after enum name");
    vm_u32 mark = p->sp;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (match(p, TOK_COMMA)) continue;
        vm_u32 vname = expect_name(p, "Expected enum variant");
        AstRef v = new_node(p, AST_VARIANT, vname);
        if (match(p, TOK_LPAREN)) {
            vm_u32 tmark = p->sp;
            if (!check(p, TOK_RPAREN)) {
                do {
                    if (is_name_type(cur(p)) && peek_at(p, 1) == TOK_COLON) { advance(p); advance(p); }
                    push(p, parse_type(p));
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN, "Expected ')' after variant payload");
            AstList payload = finish_list(p, tmark);
            N(p, v)->a = payload;
        } else if (match(p, TOK_ASSIGN)) {
            AstRef val = parse_expr(p);
            N(p, v)->b = val;
        }
        push(p, v);
    }
    expect(p, TOK_RBRACE, "Expected '}' after enum variants");
    AstList variants = finish_list(p, mark);
    N(p, en)->a = variants;
    if (match(p, TOK_AS)) N(p, en)->b = parse_ns_alias(p);
    return en;
}

static AstRef parse_import(Parser *p, vm_u32 at) {
    AstRef im = new_node(p, AST_IMPORT, at);
    vm_u32 mark = p->sp;
    if (check(p, TOK_STRING_LIT)) {
        N(p, im)->flags |= AST_F_PATH;
        push(p, advance(p));
    } else {
        do { push(p, expect_name(p, "Expected module name")); } while (match(p, TOK_DOT));
    }
    AstList path = finish_list(p, mark);
    N(p, im)->a = path;
    if (match(p, TOK_LBRACE)) {
        N(p, im)->flags |= AST_F_BRACES;
        vm_u32 imark = p->sp;
        if (!check(p, TOK_RBRACE)) {
            do {
                if (check(p, TOK_RBRACE)) break;
                vm_u32 nm = check(p, TOK_STAR) ? advance(p) : expect_name(p, "Expected imported name");
                AstRef item = new_node(p, AST_IMPORT_ITEM, nm);
                if (match(p, TOK_AS)) N(p, item)->a = expect_name(p, "Expected alias after 'as'");
                push(p, item);
            } while (match(p, TOK_COMMA));
        }
        expect(p, TOK_RBRACE, "Expected '}' after import list");
        AstList items = finish_list(p, imark);
        N(p, im)->b = items;
    }
    if (match(p, TOK_AS)) N(p, im)->c = expect_name(p, "Expected alias after 'as'");
    return im;
}

static AstRef parse_const_decl(Parser *p, vm_u16 flags) {
    vm_u32 name = expect_name(p, "Expected constant name");
    AstRef cn = new_node(p, AST_CONST, name);
    N(p, cn)->flags = flags;
    if (match(p, TOK_COLON)) {
        AstRef ty = parse_type(p);
        N(p, cn)->a = ty;
    }
    expect(p, TOK_ASSIGN, "Expected '=' in constant declaration");
    AstRef val = parse_binary(p, 1);
    N(p, cn)->b = val;
    if (match(p, TOK_AS)) N(p, cn)->c = parse_ns_alias(p);
    return cn;
}

/* `export Name [as ns]` or `export { a, b as c, d: e }` */
static AstRef parse_export_list(Parser *p, vm_u32 at) {
    AstRef ex = new_node(p, AST_EXPORT, at);
    vm_u32 mark = p->sp;
    vm_bool braces = match(p, TOK_LBRACE);
    do {
        if (braces && check(p, TOK_RBRACE)) break;
        vm_u32 first = expect_name(p, "Expected symbol name in export");
        AstRef item = new_node(p, AST_IMPORT_ITEM, first);
        if (match(p, TOK_AS)) {
            N(p, item)->a = parse_ns_alias(p);
        } else if (braces && (match(p, TOK_COLON) || match(p, TOK_ASSIGN))) {
            N(p, item)->a = first;
            N(p, item)->tok = expect_name(p, "Expected target symbol");
        }
        push(p, item);
    } while (braces && match(p, TOK_COMMA));
    if (braces) expect(p, TOK_RBRACE, "Expected '}' after export list");
    AstList items = finish_list(p, mark);
    N(p, ex)->a = items;
    return ex;
}

/* `module a.b` — `module` is a keyword only here, so it stays usable as a name elsewhere */
static vm_bool at_module_decl(const Parser *p) {
    if (cur(p) != TOK_IDENT || peek_at(p, 1) != TOK_IDENT) return VM_FALSE;
    const Token *t = tok_at(p, p->pos);
    return vm_str_eq_cstr(t->start, t->length, "module");
}

static AstRef parse_item(Parser *p) {
    AstList attrs = parse_attrs(p);
    vm_u16 flags = 0;
    vm_u32 at = p->pos;
    if (check(p, TOK_EXPORT)) {
        error(p, "'export' was removed: name the module with 'module a.b' at the top of the file and mark what it shares with 'pub'");
        return 0;
    }
    if (ast_list_len(p->t, attrs) == 0 && at_module_decl(p)) {
        advance(p);
        AstRef md = new_node(p, AST_MODULE, at);
        vm_u32 mark = p->sp;
        do { push(p, expect_name(p, "Expected module name after 'module'")); } while (match(p, TOK_DOT));
        AstList path = finish_list(p, mark);
        N(p, md)->a = path;
        match(p, TOK_SEMICOLON);
        return md;
    }
    while (match(p, TOK_PUB)) flags |= AST_F_PUB;
    at = p->pos;
    TokenType c = cur(p);
    AstRef item = 0;
    switch (c) {
        case TOK_IMPORT:
            advance(p);
            item = parse_import(p, at);
            if (flags & AST_F_PUB) N(p, item)->flags |= AST_F_PUB;   /* `pub import`: share it with our importers */
            break;
        case TOK_ENUM:     advance(p); item = parse_enum(p, flags); break;
        case TOK_TYPE:     advance(p); item = parse_type_decl(p, flags, attrs); break;
        case TOK_STRUCT:   advance(p); item = parse_struct(p, flags, attrs); break;
        case TOK_FUNC:     advance(p); item = parse_func(p, flags, attrs, vm_bool_from_int(ast_list_len(p->t, attrs) > 0)); break;
        case TOK_OPERATOR: advance(p); item = parse_operator(p, flags, attrs, VM_FALSE); break;
        case TOK_CONST:    advance(p); item = parse_const_decl(p, flags); break;
        case TOK_LET:
            if (flags & AST_F_EXPORT) { advance(p); item = parse_const_decl(p, flags); break; }
            error(p, "Expected a top-level declaration (func, struct, type, enum, import, const, operator)");
            break;
        case TOK_IDENT: case TOK_LBRACE:
            if (flags & AST_F_EXPORT) { item = parse_export_list(p, at); break; }
            error(p, "Expected a top-level declaration (func, struct, type, enum, import, const, operator)");
            break;
        default:
            error(p, "Expected a top-level declaration (func, struct, type, enum, import, const, operator)");
            break;
    }
    match(p, TOK_SEMICOLON);
    return item;
}

static vm_bool is_item_start(TokenType ty) {
    return vm_bool_from_int(ty == TOK_IMPORT || ty == TOK_EXPORT || ty == TOK_PUB || ty == TOK_ENUM ||
                            ty == TOK_TYPE || ty == TOK_STRUCT || ty == TOK_FUNC || ty == TOK_OPERATOR ||
                            ty == TOK_CONST || ty == TOK_AT_LBRACKET);
}

/* After an error: skip to the next token that starts a line with a top-level keyword */
static void synchronize(Parser *p) {
    p->panic = VM_FALSE;
    p->depth = 0;
    p->no_brace = VM_FALSE;
    p->no_method = VM_FALSE;
    p->in_command = VM_FALSE;
    if (p->pos < p->end) p->pos++;
    while (p->pos < p->end) {
        const Token *k = tok_at(p, p->pos);
        if (is_item_start(k->type) && tok_at(p, p->pos - 1)->line != k->line) return;
        p->pos++;
    }
}

/* Remembers where a top-level item's tokens are (used to print module interfaces) */
static void add_span(AstTree *t, AstRef item, vm_u32 start, vm_u32 end) {
    if (t->num_spans + 3 > t->spans_cap) {
        vm_u32 cap = (t->spans_cap > 0) ? t->spans_cap * 2 : 96;
        vm_u32 *ns = (vm_u32 *)t->realloc_fn(t->alloc_ud, t->spans, sizeof(vm_u32) * t->spans_cap, sizeof(vm_u32) * cap);
        if (ns == VM_NULL) return;
        t->spans = ns;
        t->spans_cap = cap;
    }
    t->spans[t->num_spans++] = item;
    t->spans[t->num_spans++] = start;
    t->spans[t->num_spans++] = end;
}

vm_bool ast_parse(AstTree *t, const char *src) {
    Parser p;
    p.t = t;
    p.panic = VM_FALSE;
    p.no_brace = VM_FALSE;
    p.no_method = VM_FALSE;
    p.in_command = VM_FALSE;
    p.depth = 0;
    p.stack = VM_NULL;
    p.sp = p.stack_cap = 0;

    /* Index 0 of nodes / tokens / extra is reserved so that 0 always means "none" / empty */
    Token dummy = {TOK_EOF, src, 0, 1, 0, 0.0};
    push_token(t, dummy);
    (void)new_node(&p, AST_NONE, 0);
    (void)alloc_extra(&p, 1);

    p.pos = lex_into(t, src, 1);
    p.end = t->num_toks - 1;

    AstRef unit = new_node(&p, AST_UNIT, p.pos);
    vm_u32 mark = p.sp;
    while (p.pos < p.end && !t->out_of_memory) {
        vm_u32 item_start = p.pos;
        AstRef item = parse_item(&p);
        if (p.panic) {
            synchronize(&p);
            continue;
        }
        if (item != 0) {
            push(&p, item);
            add_span(t, item, item_start, p.pos);
        }
    }
    AstList items = finish_list(&p, mark);
    t->nodes[unit].a = items;
    t->root = unit;

    t->realloc_fn(t->alloc_ud, p.stack, sizeof(vm_u32) * p.stack_cap, 0);
    if (t->out_of_memory && t->num_errors < AST_MAX_ERRORS) {
        AstError *e = &t->errors[t->num_errors++];
        e->line = 0;
        vm_str_copy(e->msg, "Out of memory while parsing", 27, sizeof(e->msg));
    }
    return vm_bool_from_int(t->num_errors == 0);
}
