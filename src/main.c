/*
* nicc — A Minimal C Compiler
*
* Compiles "MiniC" (a subset of C) to x86-64 or ARM64 assembly.
* Capstone project for the CoderMusings "Compiler Internals" series.
*
* Language: int type, arithmetic (+,-,*,/), comparisons (<,>,==,!=),
*           local variables, if/else, functions, recursion.
*
* Usage: ./minicc [-arch x86_64|arm64] input.mc
* Output: assembly written to stdout
*
* License: MIT
*/

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
* GLOBAL STATE
* ================================================================ */

typedef enum { ARCH_X86_64, ARCH_ARM64 } Arch;

static const char *src;
static int32_t pos;
static int32_t cur_line = 1;
static Token tok;
static int32_t label_id = 0;
static Arch arch = ARCH_X86_64;
static int32_t is_macos = 0;
static FILE *out;

/* Symbol table for locals */
typedef struct { char name[64]; int32_t offset; } Local;
static Local locals[128];
static int32_t local_count;
static int32_t frame_size;

/* Function registry */
typedef struct { char name[64]; int32_t params; } FuncReg;
static FuncReg funcs[64];
static int32_t func_reg_count;

/* ================================================================
* ERROR HANDLING
* ================================================================ */

static void error(const char *fmt, ...) {
  va_list ap;
  fprintf(stderr, "minicc:%d: error: ", cur_line);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "");
  exit(1);
}



/* ================================================================
* MAIN
* ================================================================ */

int32_t main(int argc, char **argv) {
  const char *infile = NULL;
  out = stdout;

  for (int32_t i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "-arch") && i + 1 < argc) {
          i++;
          if (!strcmp(argv[i], "x86_64")) arch = ARCH_X86_64;
          else if (!strcmp(argv[i], "arm64")) arch = ARCH_ARM64;
          else {
              fprintf(stderr, "unknown arch: %s", argv[i]); return 1; }
      } else if (!strcmp(argv[i], "-macos")) {
          is_macos = 1;
      } else {
          infile = argv[i];
      }
  }

  if (!infile) {
      fprintf(stderr, "Usage: minicc [-arch x86_64|arm64] input.mc");
      return 1;
  }

  /* Read input file */
  FILE *f = fopen(infile, "r");
  if (!f) { perror(infile); return 1; }
  fseek(f, 0, SEEK_END);
  int64_t len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc(len + 1);
  if (!buf) {
      fprintf(stderr, "out of memory");
      return 1;
  }
  fread(buf, 1, len, f);
  buf[len] = '�';
  fclose(f);

  /* Compile */
  src = buf;
  pos = 0;
  next_token();
  ASTNode *prog = parse_program();
  register_funcs(prog);

  if (arch == ARCH_X86_64)
      gen_x86(prog);
  else
      gen_arm(prog);

  free(buf);
  return 0;
}
