#ifndef COMPILER_LEXER_TOKEN_H
#define COMPILER_LEXER_TOKEN_H

#include "../../vm/core/vm_types.h"

typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_INT_LIT,
    TOK_FLOAT_LIT,
    TOK_STRING_LIT,
    TOK_FSTRING_LIT,

    /* Core Keywords */
    TOK_FUNC,
    TOK_LET,
    TOK_CONST,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_IN,
    TOK_RETURN,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_TRUE,
    TOK_FALSE,
    TOK_PRINT,
    TOK_LEN,

    /* Module (`import`, `export`, `as`, `pub`), Struct, Type, Operator, Self, Enum, Match, Or, Is */
    TOK_IMPORT,
    TOK_EXPORT,
    TOK_PUB,
    TOK_AS,
    TOK_IS,
    TOK_STRUCT,
    TOK_TYPE,
    TOK_OPERATOR,
    TOK_SELF_TYPE,
    TOK_ENUM,
    TOK_MATCH,
    TOK_OR,

    /* Type Keywords */
    TOK_TYPE_I32,
    TOK_TYPE_I64,
    TOK_TYPE_F32,
    TOK_TYPE_F64,
    TOK_TYPE_BOOL,
    TOK_TYPE_STRING,
    TOK_TYPE_VOID,

    /* Punctuation */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_AT_LBRACKET,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_COMMA,
    TOK_DOT,
    TOK_DOTDOT,
    TOK_ELLIPSIS,
    TOK_FAT_ARROW,
    TOK_ARROW,
    TOK_QUESTION,     /* ?   (optional type suffix) */
    TOK_QQ,           /* ??  (default for a missing optional) */
    TOK_QDOT,         /* ?.  (optional chaining) */
    TOK_HASH,

    /* Assignment & Compound Assignment Operators */
    TOK_ASSIGN,
    TOK_PLUS_EQ,
    TOK_MINUS_EQ,
    TOK_STAR_EQ,
    TOK_SLASH_EQ,
    TOK_MOD_EQ,
    TOK_BAND_EQ,
    TOK_BOR_EQ,
    TOK_BXOR_EQ,
    TOK_SHL_EQ,
    TOK_SHR_EQ,

    /* Arithmetic Operators */
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,

    /* Bitwise Operators */
    TOK_BAND,
    TOK_BOR,
    TOK_BXOR,
    TOK_BNOT,
    TOK_SHL,
    TOK_SHR,

    /* Logical & Comparison Operators */
    TOK_LAND,
    TOK_LOR,
    TOK_LNOT,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE
} TokenType;

typedef struct {
    TokenType   type;
    const char *start;
    vm_u32      length;
    vm_u32      line;
    vm_i64      int_val;
    vm_f64      float_val;
} Token;

#endif /* COMPILER_LEXER_TOKEN_H */
