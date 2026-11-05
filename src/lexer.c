#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "header/ast.h"
#include "header/codegen.h"
#include "header/lexer.h"
#include "header/parser.h"
#include "header/semantic.h"


/* ================================================================
* TOKEN TYPES
* ================================================================ */

typedef enum {
  TOK_INT, TOK_IF,TOK_ELIF, TOK_ELSE, TOK_RETURN,     /* keywords */
  TOK_IDENT, TOK_NUM,                          /* literals */
  TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH,   /* arithmetic */
  TOK_LT, TOK_GT, TOK_EQ, TOK_NEQ,            /* comparison */
  TOK_ASSIGN,                                   /* = */
  TOK_LPAREN, TOK_RPAREN,                       /* ( ) */
  TOK_LBRACE, TOK_RBRACE,                       /* { } */
  TOK_SEMI, TOK_COMMA,                          /* ; , */
  TOK_EOF
} TokenType;

typedef struct {
  TokenType type;
  char name[64];
  int32_t value;
  int32_t line;
} Token;



/* ================================================================
* LEXER
* ================================================================ */

static void skip_ws(void) {
  while (src[pos]) {
      if (src[pos] == '') { cur_line++; pos++; }
      else if (isspace((uint8_t)src[pos])) { pos++; }
      else if (src[pos] == '/' && src[pos + 1] == '/') {
          while (src[pos] && src[pos] != '') pos++;
      } else break;
  }
}

static void next_token(void) {
  skip_ws();
  tok.line = cur_line;

  if (!src[pos]) { tok.type = TOK_EOF; strcpy(tok.name, "EOF"); return; }

  /* Numbers */
  if (isdigit((uint8_t)src[pos])) {
      tok.type = TOK_NUM;
      tok.value = 0;
      while (isdigit((uint8_t)src[pos]))
          tok.value = tok.value * 10 + (src[pos++] - '0');
      snprintf(tok.name, 64, "%d", tok.value);
      return;
  }

  /* Identifiers & keywords */
  if (isalpha((uint8_t)src[pos]) || src[pos] == '_') {
      int32_t s = pos;
      while (isalnum((uint8_t)src[pos]) || src[pos] == '_') pos++;
      int32_t len = pos - s;
      if (len > 63) len = 63;
      memcpy(tok.name, src + s, len);
      tok.name[len] = '�';
      if (!strcmp(tok.name, "int"))    tok.type = TOK_INT;
      else if (!strcmp(tok.name, "if"))     tok.type = TOK_IF;
      else if (!strcmp(tok.name, "elif"))   tok.type = TOK_ELIF;
      else if (!strcmp(tok.name, "else"))   tok.type = TOK_ELSE;
      else if (!strcmp(tok.name, "return")) tok.type = TOK_RETURN;
      else tok.type = TOK_IDENT;
      return;
  }

  /* Two-char operators */
  if (src[pos] == '=' && src[pos + 1] == '=') {
      tok.type = TOK_EQ; strcpy(tok.name, "=="); pos += 2; return;
  }
  if (src[pos] == '!' && src[pos + 1] == '=') {
      tok.type = TOK_NEQ; strcpy(tok.name, "!="); pos += 2; return;
  }

  /* Single-char tokens */
  char c = src[pos++];
  tok.name[0] = c; tok.name[1] = '�';
  switch (c) {
      case '+': tok.type = TOK_PLUS;   return;
      case '-': tok.type = TOK_MINUS;  return;
      case '*': tok.type = TOK_STAR;   return;
      case '/': tok.type = TOK_SLASH;  return;
      case '<': tok.type = TOK_LT;     return;
      case '>': tok.type = TOK_GT;     return;
      case '=': tok.type = TOK_ASSIGN; return;
      case '(': tok.type = TOK_LPAREN; return;
      case ')': tok.type = TOK_RPAREN; return;
      case '{': tok.type = TOK_LBRACE; return;
      case '}': tok.type = TOK_RBRACE; return;
      case ';': tok.type = TOK_SEMI;   return;
      case ',': tok.type = TOK_COMMA;  return;
      default: error("unexpected character '%c'", c);
  }
}

static void expect(TokenType t) {
  if (tok.type != t) error("expected token type %d, got '%s'", t, tok.name);
  next_token();
}

static ASTNode *new_node(NodeType t) {
  ASTNode *n = calloc(1, sizeof(ASTNode));
  if (!n) { fprint32_tf(stderr, "out of memory"); exit(1); }
  n->type = t;
  n->line = tok.line;
  return n;
}
