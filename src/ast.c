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
* AST NODE TYPES
* ================================================================ */


typedef enum {
  NODE_PROGRAM, NODE_FUNC, NODE_BLOCK, NODE_RETURN,
  NODE_IF, NODE_VAR_DECL, NODE_ASSIGN, NODE_BINARY,
  NODE_CALL, NODE_NUM, NODE_IDENT, NODE_EXPR_STMT
} NodeType;

typedef struct ASTNode ASTNode;

struct ASTNode {
  NodeType type;
  int32_t line;
  union {
      struct { ASTNode **funcs; int32_t count; } program;
      struct { char name[64]; char params[8][64]; int32_t param_count;
               ASTNode *body; } func;
      struct { ASTNode **stmts; int32_t count; } block;
      struct { ASTNode *expr; } ret;
      struct { ASTNode *cond; ASTNode *then_body;
               ASTNode *else_body; } if_stmt;
      struct { char name[64]; ASTNode *init; } var_decl;
      struct { char name[64]; ASTNode *expr; } assign;
      struct { TokenType op; ASTNode *left; ASTNode *right; } binary;
      struct { char name[64]; ASTNode **args; int32_t arg_count; } call;
      struct { int32_t value; } num;
      struct { char name[64]; } ident;
      struct { ASTNode *expr; } expr_stmt;
  };
};
