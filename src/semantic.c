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
* SEMANTIC HELPERS
* ================================================================ */

static void emit(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(out, fmt, ap);
  va_end(ap);
  fprintf(out, "");
}

static int32_t new_label(void) { return label_id++; }

/* Platform-aware symbol name: macOS prefixes with underscore */
static char _sym_buf[128];
static const char *sym(const char *name) {
  if (is_macos) { snprintf(_sym_buf, sizeof(_sym_buf), "_%s", name); return _sym_buf; }
  return name;
}

static void register_funcs(ASTNode *prog) {
  func_reg_count = 0;
  for (int32_t i = 0; i < prog->program.count; i++) {
      strcpy(funcs[func_reg_count].name, prog->program.funcs[i]->func.name);
      funcs[func_reg_count].params = prog->program.funcs[i]->func.param_count;
      func_reg_count++;
  }
}

static int32_t find_local(const char *name) {
  for (int32_t i = 0; i < local_count; i++)
      if (!strcmp(locals[i].name, name)) return locals[i].offset;
  return 0;
}

static int32_t add_local(const char *name) {
  if (local_count >= 128) error("too many locals");
  frame_size += 4;
  strcpy(locals[local_count].name, name);
  locals[local_count].offset = frame_size;
  local_count++;
  return frame_size;
}

static void scan_locals(ASTNode *n) {
  if (!n) return;
  if (n->type == NODE_VAR_DECL) {
      add_local(n->var_decl.name);
  } else if (n->type == NODE_IF) {
      scan_locals(n->if_stmt.then_body);
      scan_locals(n->if_stmt.else_body);
  } else if (n->type == NODE_BLOCK) {
      for (int32_t i = 0; i < n->block.count; i++)
          scan_locals(n->block.stmts[i]);
  }
}
