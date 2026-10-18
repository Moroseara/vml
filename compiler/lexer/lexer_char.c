#include "lexer.h"
#include "../../vm/string/vm_string.h"

vm_bool lexer_is_alpha(char c) {
    return vm_bool_from_int((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_');
}

vm_bool lexer_is_digit(char c) {
    return vm_bool_from_int(c >= '0' && c <= '9');
}

void lexer_init(Lexer *lex, const char *src) {
    lex->src = src;
    lex->pos = 0;
    lex->line = 1;
}

/* Skips whitespace, `##` single-line comments, and `##[ ... ]` block comments */
void lexer_skip_trivia(Lexer *lex) {
    for (;;) {
        char c = lex->src[lex->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            lex->pos++;
        } else if (c == '\n') {
            lex->line++;
            lex->pos++;
        } else if (c == '#' && lex->src[lex->pos + 1] == '#') {
            if (lex->src[lex->pos + 2] == '[') {
                lex->pos += 3;
                int depth = 1;
                while (lex->src[lex->pos] != '\0' && depth > 0) {
                    if (lex->src[lex->pos] == '\n') lex->line++;
                    else if (lex->src[lex->pos] == '[') depth++;
                    else if (lex->src[lex->pos] == ']') {
                        depth--;
                        if (depth == 0) break;
                    }
                    lex->pos++;
                }
                if (lex->src[lex->pos] == ']') lex->pos++;
            } else {
                lex->pos += 2;
                while (lex->src[lex->pos] != '\0' && lex->src[lex->pos] != '\n') {
                    lex->pos++;
                }
            }
        } else {
            break;
        }
    }
}

Token lexer_scan_number(Lexer *lex, Token tok) {
    vm_size start = lex->pos;
    vm_i64 ival = 0;
    if (lex->src[lex->pos] == '0' && (lex->src[lex->pos + 1] == 'x' || lex->src[lex->pos + 1] == 'X')) {
        lex->pos += 2;
        while (lexer_is_digit(lex->src[lex->pos]) == VM_TRUE ||
               (lex->src[lex->pos] >= 'a' && lex->src[lex->pos] <= 'f') ||
               (lex->src[lex->pos] >= 'A' && lex->src[lex->pos] <= 'F') ||
               lex->src[lex->pos] == '_') {
            char ch = lex->src[lex->pos++];
            if (ch == '_') continue;
            vm_i64 d = (ch >= '0' && ch <= '9') ? (ch - '0') :
                       (ch >= 'a' && ch <= 'f') ? (ch - 'a' + 10) : (ch - 'A' + 10);
            ival = (ival << 4) | d;
        }
        tok.type = TOK_INT_LIT;
        tok.int_val = ival;
        tok.length = (vm_u32)(lex->pos - start);
        return tok;
    }
    if (lex->src[lex->pos] == '0' && (lex->src[lex->pos + 1] == 'b' || lex->src[lex->pos + 1] == 'B')) {
        lex->pos += 2;
        while (lex->src[lex->pos] == '0' || lex->src[lex->pos] == '1' || lex->src[lex->pos] == '_') {
            char ch = lex->src[lex->pos++];
            if (ch == '_') continue;
            ival = (ival << 1) | (ch - '0');
        }
        tok.type = TOK_INT_LIT;
        tok.int_val = ival;
        tok.length = (vm_u32)(lex->pos - start);
        return tok;
    }
    while (lexer_is_digit(lex->src[lex->pos]) == VM_TRUE || lex->src[lex->pos] == '_') {
        if (lex->src[lex->pos] != '_') {
            ival = ival * 10 + (lex->src[lex->pos] - '0');
        }
        lex->pos++;
    }
    /* Float: digits [. digits] [e[+-]digits] — converted exactly (no digit-by-digit rounding) */
    vm_bool is_float = VM_FALSE;
    if (lex->src[lex->pos] == '.' && lexer_is_digit(lex->src[lex->pos + 1]) == VM_TRUE) {
        is_float = VM_TRUE;
        lex->pos++;
        while (lexer_is_digit(lex->src[lex->pos]) == VM_TRUE || lex->src[lex->pos] == '_') lex->pos++;
    }
    if (lex->src[lex->pos] == 'e' || lex->src[lex->pos] == 'E') {
        vm_size k = lex->pos + 1;
        if (lex->src[k] == '+' || lex->src[k] == '-') k++;
        if (lexer_is_digit(lex->src[k]) == VM_TRUE) {
            is_float = VM_TRUE;
            lex->pos = k;
            while (lexer_is_digit(lex->src[lex->pos]) == VM_TRUE) lex->pos++;
        }
    }
    if (is_float) {
        tok.type = TOK_FLOAT_LIT;
        tok.float_val = vm_str_parse_f64(&lex->src[start], (vm_u32)(lex->pos - start));
        tok.length = (vm_u32)(lex->pos - start);
        return tok;
    }
    tok.type = TOK_INT_LIT;
    tok.int_val = ival;
    tok.length = (vm_u32)(lex->pos - start);
    return tok;
}

/*
 * Scans either:
 *   1) Raw String `""[ ... ]` (supports multi-line text, nested `[...]`, and unescaped `"` / `\`)
 *   2) Normal String `"..."`
 */
Token lexer_scan_string(Lexer *lex, Token tok) {
    if (lex->src[lex->pos] == '"' &&
        lex->src[lex->pos + 1] == '"' &&
        lex->src[lex->pos + 2] == '[') {
        lex->pos += 3;
        if (lex->src[lex->pos] == '\r') lex->pos++;
        if (lex->src[lex->pos] == '\n') {
            lex->line++;
            lex->pos++;
        }
        vm_size content_start = lex->pos;
        int depth = 1;
        while (lex->src[lex->pos] != '\0' && depth > 0) {
            char ch = lex->src[lex->pos];
            if (ch == '\n') lex->line++;
            else if (ch == '[') depth++;
            else if (ch == ']') {
                depth--;
                if (depth == 0) break;
            }
            lex->pos++;
        }
        vm_size content_end = lex->pos;
        if (content_end > content_start && lex->src[content_end - 1] == '\n') {
            content_end--;
            if (content_end > content_start && lex->src[content_end - 1] == '\r') {
                content_end--;
            }
        }
        if (lex->src[lex->pos] == ']') lex->pos++;
        tok.start = &lex->src[content_start];
        tok.length = (vm_u32)(content_end - content_start);
        tok.type = TOK_STRING_LIT;
        return tok;
    }

    char quote = lex->src[lex->pos];
    lex->pos++;
    tok.start = &lex->src[lex->pos];
    vm_size start = lex->pos;
    while (lex->src[lex->pos] != '\0' && lex->src[lex->pos] != quote) {
        if (lex->src[lex->pos] == '\\' && lex->src[lex->pos + 1] != '\0') {
            lex->pos += 2;
            continue;
        }
        if (lex->src[lex->pos] == '\n') lex->line++;
        lex->pos++;
    }
    tok.length = (vm_u32)(lex->pos - start);
    if (lex->src[lex->pos] == quote) lex->pos++;
    tok.type = TOK_STRING_LIT;
    return tok;
}
