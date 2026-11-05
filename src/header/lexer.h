#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>

/* Token Types */
typedef enum {
    TOK_INT, TOK_IF, TOK_ELIF, TOK_ELSE, TOK_RETURN,     /* keywords */
    TOK_IDENT, TOK_NUM,                                  /* literals */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH,            /* arithmetic */
    TOK_LT, TOK_GT, TOK_EQ, TOK_NEQ,                     /* comparison */
    TOK_ASSIGN,                                          /* = */
    TOK_LPAREN, TOK_RPAREN,                              /* ( ) */
    TOK_LBRACE, TOK_RBRACE,                              /* { } */
    TOK_SEMI, TOK_COMMA,                                 /* ; , */
    TOK_EOF
} TokenType;

/* Token Structure */
typedef struct {
    TokenType type;
    char name[64];
    int32_t value;
    int32_t line;
} Token;

/* Global current token (defined in lexer.c) */
extern Token tok;

/* Lexer Functions */
static void next_token(void);
static void expect(TokenType t);

#endif /* LEXER_H */