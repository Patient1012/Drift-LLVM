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
* PARSER (Recursive Descent)
* ================================================================ */

static ASTNode *parse_expr(void);
static ASTNode *parse_stmt(void);

static ASTNode *parse_primary(void) {
  if (tok.type == TOK_NUM) {
      ASTNode *n = new_node(NODE_NUM);
      n->num.value = tok.value;
      next_token();
      return n;
  }
  if (tok.type == TOK_IDENT) {
      char name[64];
      strcpy(name, tok.name);
      next_token();
      if (tok.type == TOK_LPAREN) {       /* function call */
          next_token();
          ASTNode *n = new_node(NODE_CALL);
          strcpy(n->call.name, name);
          n->call.args = malloc(8 * sizeof(ASTNode *));
          n->call.arg_count = 0;
          if (tok.type != TOK_RPAREN) {
              n->call.args[n->call.arg_count++] = parse_expr();
              while (tok.type == TOK_COMMA) {
                  next_token();
                  if (n->call.arg_count >= 8) error("too many arguments");
                  n->call.args[n->call.arg_count++] = parse_expr();
              }
          }
          expect(TOK_RPAREN);
          return n;
      }
      ASTNode *n = new_node(NODE_IDENT);  /* variable */
      strcpy(n->ident.name, name);
      return n;
  }
  if (tok.type == TOK_LPAREN) {
      next_token();
      ASTNode *n = parse_expr();
      expect(TOK_RPAREN);
      return n;
  }
  error("unexpected '%s' in expression", tok.name);
  return NULL;
}

static ASTNode *parse_unary(void) {
  if (tok.type == TOK_MINUS) {
      next_token();
      ASTNode *n = new_node(NODE_BINARY);
      n->binary.op = TOK_MINUS;
      n->binary.left = new_node(NODE_NUM);
      n->binary.left->num.value = 0;
      n->binary.right = parse_primary();
      return n;
  }
  return parse_primary();
}

static ASTNode *parse_mul(void) {
  ASTNode *left = parse_unary();
  while (tok.type == TOK_STAR || tok.type == TOK_SLASH) {
      ASTNode *n = new_node(NODE_BINARY);
      n->binary.op = tok.type;
      next_token();
      n->binary.left = left;
      n->binary.right = parse_unary();
      left = n;
  }
  return left;
}

static ASTNode *parse_add(void) {
  ASTNode *left = parse_mul();
  while (tok.type == TOK_PLUS || tok.type == TOK_MINUS) {
      ASTNode *n = new_node(NODE_BINARY);
      n->binary.op = tok.type;
      next_token();
      n->binary.left = left;
      n->binary.right = parse_mul();
      left = n;
  }
  return left;
}

/* Lowest precedence in MiniC: comparisons */
static ASTNode *parse_expr(void) {
  ASTNode *left = parse_add();
  while (tok.type == TOK_LT || tok.type == TOK_GT ||
         tok.type == TOK_EQ || tok.type == TOK_NEQ) {
      ASTNode *n = new_node(NODE_BINARY);
      n->binary.op = tok.type;
      next_token();
      n->binary.left = left;
      n->binary.right = parse_add();
      left = n;
  }
  return left;
}

static ASTNode *parse_block_body(void) {
  ASTNode *b = new_node(NODE_BLOCK);
  b->block.stmts = malloc(128 * sizeof(ASTNode *));
  b->block.count = 0;
  while (tok.type != TOK_RBRACE && tok.type != TOK_EOF) {
      if (b->block.count >= 128) error("too many statements");
      b->block.stmts[b->block.count++] = parse_stmt();
  }
  return b;
}

static ASTNode *parse_stmt(void) {
  if (tok.type == TOK_RETURN) {
      next_token();
      ASTNode *n = new_node(NODE_RETURN);
      n->ret.expr = parse_expr();
      expect(TOK_SEMI);
      return n;
  }
  if (tok.type == TOK_IF) {
      next_token();
      ASTNode *n = new_node(NODE_IF);
      expect(TOK_LPAREN);
      n->if_stmt.cond = parse_expr();
      expect(TOK_RPAREN);
      expect(TOK_LBRACE);
      n->if_stmt.then_body = parse_block_body();
      expect(TOK_RBRACE);
      n->if_stmt.else_body = NULL;
      if (tok.type == TOK_ELSE) {
          next_token();
          expect(TOK_LBRACE);
          n->if_stmt.else_body = parse_block_body();
          expect(TOK_RBRACE);
      }
      return n;
  }
  if (tok.type == TOK_INT) {
      next_token();
      ASTNode *n = new_node(NODE_VAR_DECL);
      if (tok.type != TOK_IDENT) error("expected variable name");
      strcpy(n->var_decl.name, tok.name);
      next_token();
      n->var_decl.init = NULL;
      if (tok.type == TOK_ASSIGN) {
          next_token();
          n->var_decl.init = parse_expr();
      }
      expect(TOK_SEMI);
      return n;
  }
  if (tok.type == TOK_IDENT) {
      char name[64];
      strcpy(name, tok.name);
      int32_t sp = pos, sl = cur_line;
      Token st = tok;
      next_token();
      if (tok.type == TOK_ASSIGN) {
          next_token();
          ASTNode *n = new_node(NODE_ASSIGN);
          strcpy(n->assign.name, name);
          n->assign.expr = parse_expr();
          expect(TOK_SEMI);
          return n;
      }
      pos = sp; cur_line = sl; tok = st;  /* backtrack */
  }
  ASTNode *n = new_node(NODE_EXPR_STMT);
  n->expr_stmt.expr = parse_expr();
  expect(TOK_SEMI);
  return n;
}

static ASTNode *parse_function(void) {
  expect(TOK_INT);
  ASTNode *n = new_node(NODE_FUNC);
  if (tok.type != TOK_IDENT) error("expected function name");
  strcpy(n->func.name, tok.name);
  next_token();
  expect(TOK_LPAREN);
  n->func.param_count = 0;
  if (tok.type != TOK_RPAREN) {
      expect(TOK_INT);
      if (tok.type != TOK_IDENT) error("expected parameter name");
      strcpy(n->func.params[n->func.param_count++], tok.name);
      next_token();
      while (tok.type == TOK_COMMA) {
          next_token();
          expect(TOK_INT);
          if (n->func.param_count >= 8) error("too many parameters");
          if (tok.type != TOK_IDENT) error("expected parameter name");
          strcpy(n->func.params[n->func.param_count++], tok.name);
          next_token();
      }
  }
  expect(TOK_RPAREN);
  expect(TOK_LBRACE);
  n->func.body = parse_block_body();
  expect(TOK_RBRACE);
  return n;
}

static ASTNode *parse_program(void) {
  ASTNode *n = new_node(NODE_PROGRAM);
  n->program.funcs = malloc(64 * sizeof(ASTNode *));
  n->program.count = 0;
  while (tok.type != TOK_EOF) {
      if (n->program.count >= 64) error("too many functions");
      n->program.funcs[n->program.count++] = parse_function();
  }
  return n;
}
