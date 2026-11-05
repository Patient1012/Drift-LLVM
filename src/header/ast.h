#ifndef AST_H
#define AST_H

#include <stdint.h>
#include "lexer.h"

/* Forward declarations */
typedef struct ASTNode ASTNode;

/* AST Node Types */
typedef enum {
    NODE_PROGRAM,
    NODE_FUNC,
    NODE_BLOCK,
    NODE_RETURN,
    NODE_IF,
    NODE_VAR_DECL,
    NODE_ASSIGN,
    NODE_BINARY,
    NODE_CALL,
    NODE_NUM,
    NODE_IDENT,
    NODE_EXPR_STMT
} NodeType;

/* AST Node Structure */
struct ASTNode {
    NodeType type;
    int32_t line;
    union {
        struct { ASTNode **funcs; int32_t count; } program;
        struct { char name[64]; char params[8][64]; int32_t param_count; ASTNode *body; } func;
        struct { ASTNode **stmts; int32_t count; } block;
        struct { ASTNode *expr; } ret;
        struct { ASTNode *cond; ASTNode *then_body; ASTNode *else_body; } if_stmt;
        struct { char name[64]; ASTNode *init; } var_decl;
        struct { char name[64]; ASTNode *expr; } assign;
        struct { TokenType op; ASTNode *left; ASTNode *right; } binary;
        struct { char name[64]; ASTNode **args; int32_t arg_count; } call;
        struct { int32_t value; } num;
        struct { char name[64]; } ident;
        struct { ASTNode *expr; } expr_stmt;
    };
};

/* AST Constructor */
ASTNode *new_node(NodeType t);

#endif /* AST_H */