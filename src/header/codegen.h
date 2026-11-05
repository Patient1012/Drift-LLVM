#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include <stdint.h>

/* Forward declarations */
typedef struct ASTNode ASTNode;

/* External globals from main.c */
extern int32_t is_macos;
extern FILE *out;

/* Architecture enum */
typedef enum { ARCH_X86_64, ARCH_ARM64 } Arch;
extern Arch arch;

/* Code Generation Functions */
static void gen_x86(ASTNode *prog);
static void gen_arm(ASTNode *prog);
static void gen_x86_func(ASTNode *fn);
static void gen_arm_func(ASTNode *fn);
static void gen_x86_stmt(ASTNode *n);
static void gen_arm_stmt(ASTNode *n);
static void gen_x86_expr(ASTNode *n);
static void gen_arm_expr(ASTNode *n);

#endif /* CODEGEN_H */