#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>

/* Forward declarations */
typedef struct ASTNode ASTNode;

/* Parser Functions */
ASTNode *parse_program(void);
ASTNode *parse_function(void);
ASTNode *parse_block_body(void);
ASTNode *parse_stmt(void);
ASTNode *parse_expr(void);
ASTNode *parse_add(void);
ASTNode *parse_mul(void);
ASTNode *parse_unary(void);
ASTNode *parse_primary(void);

#endif /* PARSER_H */