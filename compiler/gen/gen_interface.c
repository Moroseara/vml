#include "gen.h"

/*
 * Module interface: the public declarations of the compiled file as `.vml` text, with every
 * function body removed (`func area(s: Shape): float`). Stored in the chunk and written at the
 * front of `.vmb` images, so tools read types without loading code (`vml interface x.vmb`).
 *
 * The text is copied from the source, so it keeps the exact types (`string[]`, `T ! E`, `<T>`)
 * and any comments inside the declarations.
 */

typedef struct {
    char  *buf;
    vm_u32 len, cap;
    VMChunk *chunk;
    vm_bool oom;
} Text;

static void put(Text *t, const char *s, vm_u32 n) {
    if (t->oom || n == 0) return;
    if (t->len + n + 1 > t->cap) {
        vm_u32 cap = (t->cap > 0) ? t->cap : 1024;
        while (cap < t->len + n + 1) cap *= 2;
        char *nb = (char *)t->chunk->realloc_fn(t->chunk->alloc_ud, t->buf, t->cap, cap);
        if (nb == VM_NULL) {
            t->oom = VM_TRUE;
            return;
        }
        t->buf = nb;
        t->cap = cap;
    }
    for (vm_u32 i = 0; i < n; i++) t->buf[t->len++] = s[i];
    t->buf[t->len] = '\0';
}

static void put_cstr(Text *t, const char *s) {
    put(t, s, vm_str_len(s));
}

static void trim_trailing(Text *t) {
    while (t->len > 0 && (t->buf[t->len - 1] == ' ' || t->buf[t->len - 1] == '\t' || t->buf[t->len - 1] == '\n' ||
                          t->buf[t->len - 1] == '\r')) {
        t->len--;
    }
    if (t->buf) t->buf[t->len] = '\0';
}

/* Token index of the `}` matching the `{` at `open` */
static vm_u32 matching_brace(const AstTree *tree, vm_u32 open, vm_u32 limit) {
    int depth = 0;
    for (vm_u32 i = open; i < limit; i++) {
        TokenType ty = tree->toks[i].type;
        if (ty == TOK_LBRACE) depth++;
        else if (ty == TOK_RBRACE && --depth == 0) return i;
    }
    return limit - 1;
}

/* body[tok] = 1 when token `tok` is the `{` opening a (non-lambda) function body */
static vm_u8 *mark_body_braces(VMChunk *chunk, const AstTree *tree) {
    vm_u8 *body = (vm_u8 *)chunk->realloc_fn(chunk->alloc_ud, VM_NULL, 0, tree->num_toks + 1);
    if (body == VM_NULL) return VM_NULL;
    for (vm_u32 i = 0; i <= tree->num_toks; i++) body[i] = 0;
    for (vm_u32 n = 1; n < tree->num_nodes; n++) {
        const AstNode *nd = &tree->nodes[n];
        if (nd->kind != AST_FUNC || nd->b == 0 || (nd->flags & AST_F_LAMBDA)) continue;
        vm_u32 bt = tree->nodes[nd->b].tok;
        if (bt < tree->num_toks && tree->toks[bt].type == TOK_LBRACE) body[bt] = 1;
    }
    return body;
}

static vm_bool is_main(const AstTree *tree, const AstNode *nd) {
    if (nd->kind != AST_FUNC) return VM_FALSE;
    const Token *k = &tree->toks[nd->tok];
    return vm_str_eq_cstr(k->start, k->length, "main");
}

/* Copies tokens [start, end) as source text, replacing function bodies by nothing */
static void copy_decl(Text *t, const AstTree *tree, const vm_u8 *body, vm_u32 start, vm_u32 end) {
    if (start >= end) return;
    const char *from = tree->toks[start].start;
    for (vm_u32 i = start; i < end; i++) {
        if (body[i]) {
            put(t, from, (vm_u32)(tree->toks[i].start - from));
            trim_trailing(t);
            vm_u32 close = matching_brace(tree, i, end);
            i = close;
            from = tree->toks[close].start + tree->toks[close].length;
        }
    }
    const Token *last = &tree->toks[end - 1];
    const char *stop = last->start + last->length;
    if (stop > from) put(t, from, (vm_u32)(stop - from));
    trim_trailing(t);
    put_cstr(t, "\n\n");
}

void gen_build_interface(Gen *g, const AstTree *tree, const char *file_name) {
    VMChunk *chunk = g->c->chunk;
    Text t = {VM_NULL, 0, 0, chunk, VM_FALSE};

    /* With any pub / export item, only those are the interface; otherwise every declaration is */
    vm_bool any_pub = VM_FALSE;
    for (vm_u32 s = 0; s + 2 < tree->num_spans; s += 3) {
        const AstNode *nd = &tree->nodes[tree->spans[s]];
        if (nd->kind != AST_IMPORT && (nd->flags & (AST_F_PUB | AST_F_EXPORT))) any_pub = VM_TRUE;
    }

    put_cstr(&t, "## Interface of ");
    put_cstr(&t, file_name);
    put_cstr(&t, " (generated; do not edit)\n\n");
    vm_u32 header_len = t.len;
    vm_u8 *body = mark_body_braces(chunk, tree);
    if (body == VM_NULL) t.oom = VM_TRUE;
    for (vm_u32 s = 0; s + 2 < tree->num_spans; s += 3) {
        const AstNode *nd = &tree->nodes[tree->spans[s]];
        switch ((AstKind)nd->kind) {
            case AST_FUNC: case AST_STRUCT: case AST_ENUM: case AST_CONST: case AST_TYPE_ALIAS: case AST_CONTRACT:
                break;
            default:
                continue;
        }
        if (is_main(tree, nd)) continue;
        if (any_pub && !(nd->flags & (AST_F_PUB | AST_F_EXPORT))) continue;
        if (body != VM_NULL) copy_decl(&t, tree, body, tree->spans[s + 1], tree->spans[s + 2]);
    }
    if (t.len > header_len && !t.oom) {
        trim_trailing(&t);
        put_cstr(&t, "\n");
        vm_chunk_set_interface(chunk, t.buf, t.len);
    } else {
        vm_chunk_set_interface(chunk, VM_NULL, 0);
    }
    if (body != VM_NULL) chunk->realloc_fn(chunk->alloc_ud, body, tree->num_toks + 1, 0);
    if (t.buf) chunk->realloc_fn(chunk->alloc_ud, t.buf, t.cap, 0);
}
