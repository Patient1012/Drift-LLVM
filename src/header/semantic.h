#ifndef SEMANTIC_H
#define SEMANTIC_H

#include <stdio.h>
#include <stdint.h>

/* Forward declarations */
typedef struct ASTNode ASTNode;

/* External globals from main.c */
extern int32_t is_macos;
extern FILE *out;

/* Semantic Analysis Functions */
static void register_funcs(ASTNode *prog);
static void scan_locals(ASTNode *n);
static int32_t find_local(const char *name);
static int32_t add_local(const char *name);
static int32_t new_label(void);
static const char *sym(const char *name);
static void emit(const char *fmt, ...);

#endif /* SEMANTIC_H */