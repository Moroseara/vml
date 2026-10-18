#include "lexer.h"
#include "../../vm/string/vm_string.h"

static TokenType match_keyword(const char *s, vm_u32 l) {
    if (vm_str_eq_cstr(s, l, "func") == VM_TRUE)     return TOK_FUNC;
    if (vm_str_eq_cstr(s, l, "let") == VM_TRUE)      return TOK_LET;
    if (vm_str_eq_cstr(s, l, "const") == VM_TRUE)    return TOK_CONST;
    if (vm_str_eq_cstr(s, l, "if") == VM_TRUE)       return TOK_IF;
    if (vm_str_eq_cstr(s, l, "else") == VM_TRUE)     return TOK_ELSE;
    if (vm_str_eq_cstr(s, l, "while") == VM_TRUE)    return TOK_WHILE;
    if (vm_str_eq_cstr(s, l, "for") == VM_TRUE)      return TOK_FOR;
    if (vm_str_eq_cstr(s, l, "in") == VM_TRUE)       return TOK_IN;
    if (vm_str_eq_cstr(s, l, "return") == VM_TRUE)   return TOK_RETURN;
    if (vm_str_eq_cstr(s, l, "break") == VM_TRUE)    return TOK_BREAK;
    if (vm_str_eq_cstr(s, l, "continue") == VM_TRUE) return TOK_CONTINUE;
    if (vm_str_eq_cstr(s, l, "true") == VM_TRUE)     return TOK_TRUE;
    if (vm_str_eq_cstr(s, l, "false") == VM_TRUE)    return TOK_FALSE;
    if (vm_str_eq_cstr(s, l, "print") == VM_TRUE)    return TOK_PRINT;
    if (vm_str_eq_cstr(s, l, "len") == VM_TRUE)      return TOK_LEN;
    if (vm_str_eq_cstr(s, l, "import") == VM_TRUE)   return TOK_IMPORT;
    if (vm_str_eq_cstr(s, l, "export") == VM_TRUE)   return TOK_EXPORT;
    if (vm_str_eq_cstr(s, l, "pub") == VM_TRUE)      return TOK_PUB;
    if (vm_str_eq_cstr(s, l, "as") == VM_TRUE)       return TOK_AS;
    if (vm_str_eq_cstr(s, l, "is") == VM_TRUE)       return TOK_IS;
    if (vm_str_eq_cstr(s, l, "struct") == VM_TRUE)   return TOK_STRUCT;
    if (vm_str_eq_cstr(s, l, "type") == VM_TRUE)     return TOK_TYPE;
    if (vm_str_eq_cstr(s, l, "operator") == VM_TRUE) return TOK_OPERATOR;
    if (vm_str_eq_cstr(s, l, "Self") == VM_TRUE)     return TOK_SELF_TYPE;
    if (vm_str_eq_cstr(s, l, "enum") == VM_TRUE)     return TOK_ENUM;
    if (vm_str_eq_cstr(s, l, "match") == VM_TRUE)    return TOK_MATCH;
    if (vm_str_eq_cstr(s, l, "or") == VM_TRUE)       return TOK_OR;
    if (vm_str_eq_cstr(s, l, "i32") == VM_TRUE || vm_str_eq_cstr(s, l, "int") == VM_TRUE) return TOK_TYPE_I32;
    if (vm_str_eq_cstr(s, l, "i64") == VM_TRUE)      return TOK_TYPE_I64;
    if (vm_str_eq_cstr(s, l, "f32") == VM_TRUE || vm_str_eq_cstr(s, l, "float") == VM_TRUE) return TOK_TYPE_F32;
    if (vm_str_eq_cstr(s, l, "f64") == VM_TRUE)      return TOK_TYPE_F64;
    if (vm_str_eq_cstr(s, l, "bool") == VM_TRUE)     return TOK_TYPE_BOOL;
    if (vm_str_eq_cstr(s, l, "string") == VM_TRUE)   return TOK_TYPE_STRING;
    if (vm_str_eq_cstr(s, l, "void") == VM_TRUE)     return TOK_TYPE_VOID;
    return TOK_IDENT;
}

Token lexer_next(Lexer *lex) {
    lexer_skip_trivia(lex);

    Token tok = {TOK_EOF, &lex->src[lex->pos], 0, lex->line, 0, 0.0};
    char c = lex->src[lex->pos];
    if (c == '\0') return tok;

    if (c == 'f' && (lex->src[lex->pos + 1] == '"' || lex->src[lex->pos + 1] == '\'')) {
        lex->pos++;
        tok = lexer_scan_string(lex, tok);
        tok.type = TOK_FSTRING_LIT;
        return tok;
    }

    if (lexer_is_alpha(c) == VM_TRUE) {
        vm_size start = lex->pos;
        while (lexer_is_alpha(lex->src[lex->pos]) == VM_TRUE ||
               lexer_is_digit(lex->src[lex->pos]) == VM_TRUE) {
            lex->pos++;
        }
        tok.length = (vm_u32)(lex->pos - start);
        tok.type = match_keyword(tok.start, tok.length);
        return tok;
    }

    if (lexer_is_digit(c) == VM_TRUE) return lexer_scan_number(lex, tok);
    if (c == '"' || c == '\'') return lexer_scan_string(lex, tok);

    lex->pos++;
    tok.length = 1;
    switch (c) {
        case '(': tok.type = TOK_LPAREN; break;
        case ')': tok.type = TOK_RPAREN; break;
        case '{': tok.type = TOK_LBRACE; break;
        case '}': tok.type = TOK_RBRACE; break;
        case '[': tok.type = TOK_LBRACKET; break;
        case ']': tok.type = TOK_RBRACKET; break;
        case '#': tok.type = TOK_HASH; break;
        case '@':
            if (lex->src[lex->pos] == '[') {
                lex->pos++; tok.length = 2; tok.type = TOK_AT_LBRACKET;
            } else {
                tok.type = TOK_AT_LBRACKET;
            }
            break;
        case ':': tok.type = TOK_COLON; break;
        case ';': tok.type = TOK_SEMICOLON; break;
        case ',': tok.type = TOK_COMMA; break;
        case '~': tok.type = TOK_BNOT; break;
        case '?':
            if (lex->src[lex->pos] == '?') { lex->pos++; tok.length = 2; tok.type = TOK_QQ; }
            else if (lex->src[lex->pos] == '.') { lex->pos++; tok.length = 2; tok.type = TOK_QDOT; }
            else tok.type = TOK_QUESTION;
            break;
        case '.':
            if (lex->src[lex->pos] == '.' && lex->src[lex->pos + 1] == '.') {
                lex->pos += 2; tok.length = 3; tok.type = TOK_ELLIPSIS;
            } else if (lex->src[lex->pos] == '.') {
                lex->pos++; tok.length = 2; tok.type = TOK_DOTDOT;
            } else tok.type = TOK_DOT;
            break;
        case '+':
            if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_PLUS_EQ; }
            else tok.type = TOK_PLUS;
            break;
        case '-':
            if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_MINUS_EQ; }
            else if (lex->src[lex->pos] == '>') { lex->pos++; tok.length = 2; tok.type = TOK_ARROW; }
            else tok.type = TOK_MINUS;
            break;
        case '*':
            if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_STAR_EQ; }
            else tok.type = TOK_STAR;
            break;
        case '/':
            if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_SLASH_EQ; }
            else tok.type = TOK_SLASH;
            break;
        case '%':
            if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_MOD_EQ; }
            else tok.type = TOK_PERCENT;
            break;
        case '&':
            if (lex->src[lex->pos] == '&') { lex->pos++; tok.length = 2; tok.type = TOK_LAND; }
            else if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_BAND_EQ; }
            else tok.type = TOK_BAND;
            break;
        case '|':
            if (lex->src[lex->pos] == '|') { lex->pos++; tok.length = 2; tok.type = TOK_LOR; }
            else if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_BOR_EQ; }
            else tok.type = TOK_BOR;
            break;
        case '^':
            if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_BXOR_EQ; }
            else tok.type = TOK_BXOR;
            break;
        case '=':
            if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_EQ; }
            else if (lex->src[lex->pos] == '>') { lex->pos++; tok.length = 2; tok.type = TOK_FAT_ARROW; }
            else tok.type = TOK_ASSIGN;
            break;
        case '!':
            if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_NE; }
            else tok.type = TOK_LNOT;
            break;
        case '<':
            if (lex->src[lex->pos] == '<') {
                lex->pos++;
                if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 3; tok.type = TOK_SHL_EQ; }
                else { tok.length = 2; tok.type = TOK_SHL; }
            } else if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_LE; }
            else tok.type = TOK_LT;
            break;
        case '>':
            if (lex->src[lex->pos] == '>') {
                lex->pos++;
                if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 3; tok.type = TOK_SHR_EQ; }
                else { tok.length = 2; tok.type = TOK_SHR; }
            } else if (lex->src[lex->pos] == '=') { lex->pos++; tok.length = 2; tok.type = TOK_GE; }
            else tok.type = TOK_GT;
            break;
        default: tok.type = TOK_EOF; break;
    }
    return tok;
}

Token lexer_peek(Lexer *lex) {
    Lexer copy = *lex;
    return lexer_next(&copy);
}
