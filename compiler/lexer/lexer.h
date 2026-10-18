#ifndef COMPILER_LEXER_H
#define COMPILER_LEXER_H

#include "token.h"

typedef struct {
    const char *src;
    vm_size     pos;
    vm_u32      line;
} Lexer;

void    lexer_init(Lexer *lex, const char *src);
Token   lexer_next(Lexer *lex);
Token   lexer_peek(Lexer *lex);

/* Internal character/literal scanners (`lexer_char.c`) */
vm_bool lexer_is_alpha(char c);
vm_bool lexer_is_digit(char c);
void    lexer_skip_trivia(Lexer *lex);
Token   lexer_scan_number(Lexer *lex, Token tok);
Token   lexer_scan_string(Lexer *lex, Token tok);

#endif /* COMPILER_LEXER_H */
