nicc — A Minimal C Compiler
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
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ================================================================
* SECTION 1: TOKEN TYPES
* ================================================================ */

typedef enum {
  TOK_INT, TOK_IF, TOK_ELSE, TOK_RETURN,     /* keywords */
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
  int value;
  int line;
} Token;

/* ================================================================
* SECTION 2: AST NODE TYPES
* ================================================================ */

typedef enum {
  NODE_PROGRAM, NODE_FUNC, NODE_BLOCK, NODE_RETURN,
  NODE_IF, NODE_VAR_DECL, NODE_ASSIGN, NODE_BINARY,
  NODE_CALL, NODE_NUM, NODE_IDENT, NODE_EXPR_STMT
} NodeType;

typedef struct ASTNode ASTNode;

struct ASTNode {
  NodeType type;
  int line;
  union {
      struct { ASTNode **funcs; int count; } program;
      struct { char name[64]; char params[8][64]; int param_count;
               ASTNode *body; } func;
      struct { ASTNode **stmts; int count; } block;
      struct { ASTNode *expr; } ret;
      struct { ASTNode *cond; ASTNode *then_body;
               ASTNode *else_body; } if_stmt;
      struct { char name[64]; ASTNode *init; } var_decl;
      struct { char name[64]; ASTNode *expr; } assign;
      struct { TokenType op; ASTNode *left; ASTNode *right; } binary;
      struct { char name[64]; ASTNode **args; int arg_count; } call;
      struct { int value; } num;
      struct { char name[64]; } ident;
      struct { ASTNode *expr; } expr_stmt;
  };
};

/* ================================================================
* SECTION 3: GLOBAL STATE
* ================================================================ */

typedef enum { ARCH_X86_64, ARCH_ARM64 } Arch;

static const char *src;
static int pos;
static int cur_line = 1;
static Token tok;
static int label_id = 0;
static Arch arch = ARCH_X86_64;
static int is_macos = 0;
static FILE *out;

/* Symbol table for locals */
typedef struct { char name[64]; int offset; } Local;
static Local locals[128];
static int local_count;
static int frame_size;

/* Function registry */
typedef struct { char name[64]; int params; } FuncReg;
static FuncReg funcs[64];
static int func_reg_count;

/* ================================================================
* SECTION 4: ERROR HANDLING
* ================================================================ */

static void error(const char *fmt, ...) {
  va_list ap;
  fprintf(stderr, "minicc:%d: error: ", cur_line);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "
");
  exit(1);
}

/* ================================================================
* SECTION 5: LEXER
* ================================================================ */

static void skip_ws(void) {
  while (src[pos]) {
      if (src[pos] == '
') { cur_line++; pos++; }
      else if (isspace((unsigned char)src[pos])) { pos++; }
      else if (src[pos] == '/' && src[pos + 1] == '/') {
          while (src[pos] && src[pos] != '
') pos++;
      } else break;
  }
}

static void next_token(void) {
  skip_ws();
  tok.line = cur_line;

  if (!src[pos]) { tok.type = TOK_EOF; strcpy(tok.name, "EOF"); return; }

  /* Numbers */
  if (isdigit((unsigned char)src[pos])) {
      tok.type = TOK_NUM;
      tok.value = 0;
      while (isdigit((unsigned char)src[pos]))
          tok.value = tok.value * 10 + (src[pos++] - '0');
      snprintf(tok.name, 64, "%d", tok.value);
      return;
  }

  /* Identifiers & keywords */
  if (isalpha((unsigned char)src[pos]) || src[pos] == '_') {
      int s = pos;
      while (isalnum((unsigned char)src[pos]) || src[pos] == '_') pos++;
      int len = pos - s;
      if (len > 63) len = 63;
      memcpy(tok.name, src + s, len);
      tok.name[len] = '�';
      if (!strcmp(tok.name, "int"))    tok.type = TOK_INT;
      else if (!strcmp(tok.name, "if"))     tok.type = TOK_IF;
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
  if (!n) { fprintf(stderr, "out of memory
"); exit(1); }
  n->type = t;
  n->line = tok.line;
  return n;
}

/* ================================================================
* SECTION 6: PARSER (Recursive Descent)
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
      int sp = pos, sl = cur_line;
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

/* ================================================================
* SECTION 7: SEMANTIC HELPERS
* ================================================================ */

static void emit(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(out, fmt, ap);
  va_end(ap);
  fprintf(out, "
");
}

static int new_label(void) { return label_id++; }

/* Platform-aware symbol name: macOS prefixes with underscore */
static char _sym_buf[128];
static const char *sym(const char *name) {
  if (is_macos) { snprintf(_sym_buf, sizeof(_sym_buf), "_%s", name); return _sym_buf; }
  return name;
}

static void register_funcs(ASTNode *prog) {
  func_reg_count = 0;
  for (int i = 0; i < prog->program.count; i++) {
      strcpy(funcs[func_reg_count].name, prog->program.funcs[i]->func.name);
      funcs[func_reg_count].params = prog->program.funcs[i]->func.param_count;
      func_reg_count++;
  }
}

static int find_local(const char *name) {
  for (int i = 0; i < local_count; i++)
      if (!strcmp(locals[i].name, name)) return locals[i].offset;
  return 0;
}

static int add_local(const char *name) {
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
      for (int i = 0; i < n->block.count; i++)
          scan_locals(n->block.stmts[i]);
  }
}

/* ================================================================
* SECTION 8: x86-64 CODE GENERATOR
* ================================================================ */

static void gen_x86_expr(ASTNode *n);
static void gen_x86_stmt(ASTNode *n);

static void gen_x86_expr(ASTNode *n) {
  switch (n->type) {
  case NODE_NUM:
      emit("    mov     eax, %d", n->num.value);
      break;
  case NODE_IDENT: {
      int off = find_local(n->ident.name);
      if (!off) error("undefined variable '%s'", n->ident.name);
      emit("    mov     eax, DWORD PTR [rbp-%d]", off);
      break;
  }
  case NODE_BINARY:
      gen_x86_expr(n->binary.left);
      emit("    push    rax");
      gen_x86_expr(n->binary.right);
      emit("    mov     ecx, eax");
      emit("    pop     rax");
      switch (n->binary.op) {
      case TOK_PLUS:  emit("    add     eax, ecx"); break;
      case TOK_MINUS: emit("    sub     eax, ecx"); break;
      case TOK_STAR:  emit("    imul    eax, ecx"); break;
      case TOK_SLASH: emit("    cdq"); emit("    idiv    ecx"); break;
      case TOK_LT:
          emit("    cmp     eax, ecx");
          emit("    setl    al");
          emit("    movzx   eax, al");
          break;
      case TOK_GT:
          emit("    cmp     eax, ecx");
          emit("    setg    al");
          emit("    movzx   eax, al");
          break;
      case TOK_EQ:
          emit("    cmp     eax, ecx");
          emit("    sete    al");
          emit("    movzx   eax, al");
          break;
      case TOK_NEQ:
          emit("    cmp     eax, ecx");
          emit("    setne   al");
          emit("    movzx   eax, al");
          break;
      default: error("unknown binary op");
      }
      break;
  case NODE_CALL: {
      const char *regs[] = {"edi","esi","edx","ecx","r8d","r9d"};
      /* Push args in reverse to save on stack */
      for (int i = n->call.arg_count - 1; i >= 0; i--) {
          gen_x86_expr(n->call.args[i]);
          emit("    push    rax");
      }
      /* Pop into ABI registers */
      for (int i = 0; i < n->call.arg_count && i < 6; i++)
          emit("    pop     %s", (i < 4) ? (const char*[]){"rdi","rsi","rdx","rcx"}[i] : (i==4?"r8":"r9"));
      /* Note: System V ABI requires 16-byte stack alignment at calls. 
         Pushing intermediate expr results violates this. Works in minicc but unsafe for external libs. */
      emit("    xor     eax, eax");  /* zero AL for variadic */
      emit("    call    %s", sym(n->call.name));
      break;
  }
  default: error("unexpected node in expression");
  }
}

static void gen_x86_stmt(ASTNode *n) {
  switch (n->type) {
  case NODE_RETURN:
      gen_x86_expr(n->ret.expr);
      emit("    leave");
      emit("    ret");
      break;
  case NODE_VAR_DECL: {
      int off = add_local(n->var_decl.name);
      if (n->var_decl.init) {
          gen_x86_expr(n->var_decl.init);
          emit("    mov     DWORD PTR [rbp-%d], eax", off);
      }
      break;
  }
  case NODE_ASSIGN: {
      int off = find_local(n->assign.name);
      if (!off) error("undefined variable '%s'", n->assign.name);
      gen_x86_expr(n->assign.expr);
      emit("    mov     DWORD PTR [rbp-%d], eax", off);
      break;
  }
  case NODE_IF: {
      int lelse = new_label(), lend = new_label();
      gen_x86_expr(n->if_stmt.cond);
      emit("    test    eax, eax");
      emit("    je      .L%d", n->if_stmt.else_body ? lelse : lend);
      for (int i = 0; i < n->if_stmt.then_body->block.count; i++)
          gen_x86_stmt(n->if_stmt.then_body->block.stmts[i]);
      if (n->if_stmt.else_body) {
          emit("    jmp     .L%d", lend);
          emit(".L%d:", lelse);
          for (int i = 0; i < n->if_stmt.else_body->block.count; i++)
              gen_x86_stmt(n->if_stmt.else_body->block.stmts[i]);
      }
      emit(".L%d:", lend);
      break;
  }
  case NODE_EXPR_STMT:
      gen_x86_expr(n->expr_stmt.expr);
      break;
  case NODE_BLOCK:
      for (int i = 0; i < n->block.count; i++)
          gen_x86_stmt(n->block.stmts[i]);
      break;
  default: error("unexpected statement type");
  }
}

static void gen_x86_func(ASTNode *fn) {
  const char *regs[] = {"edi","esi","edx","ecx","r8d","r9d"};
  local_count = 0;
  frame_size = 0;

  /* Register params as locals first (pre-scan for stack size) */
  for (int i = 0; i < fn->func.param_count; i++)
      add_local(fn->func.params[i]);

  /* Pre-scan body for variable declarations to calculate frame size */
  scan_locals(fn->func.body);
  int total_frame = (frame_size + 15) & ~15; /* align to 16 */
  /* Reset locals — they'll be re-added during codegen */
  local_count = 0; frame_size = 0;
  for (int i = 0; i < fn->func.param_count; i++)
      add_local(fn->func.params[i]);

  emit("");
  emit("    .globl  %s", sym(fn->func.name));
  if (!is_macos) emit("    .type   %s, @function", sym(fn->func.name));
  emit("%s:", sym(fn->func.name));
  emit("    push    rbp");
  emit("    mov     rbp, rsp");
  if (total_frame > 0)
      emit("    sub     rsp, %d", total_frame);

  /* Store params from registers to stack */
  for (int i = 0; i < fn->func.param_count && i < 6; i++)
      emit("    mov     DWORD PTR [rbp-%d], %s", locals[i].offset, regs[i]);

  /* Generate body */
  for (int i = 0; i < fn->func.body->block.count; i++)
      gen_x86_stmt(fn->func.body->block.stmts[i]);

  /* Implicit return 0 */
  emit("    xor     eax, eax");
  emit("    leave");
  emit("    ret");
}

static void gen_x86(ASTNode *prog) {
  emit("    .intel_syntax noprefix");
  emit("    .text");
  for (int i = 0; i < prog->program.count; i++)
      gen_x86_func(prog->program.funcs[i]);
  if (!is_macos) {
      emit("");
      emit("    .section .note.GNU-stack,"",@progbits");
  }
}

/* ================================================================
* SECTION 9: ARM64 CODE GENERATOR (AAPCS64)
* ================================================================ */

static void gen_arm_expr(ASTNode *n);
static void gen_arm_stmt(ASTNode *n);

static void gen_arm_expr(ASTNode *n) {
  switch (n->type) {
  case NODE_NUM:
      if (n->num.value >= 0 && n->num.value <= 65535)
          emit("    mov     w0, #%d", n->num.value);
      else {
          emit("    mov     w0, #%d", n->num.value & 0xFFFF);
          if (n->num.value >> 16)
              emit("    movk    w0, #%d, lsl #16", (n->num.value >> 16) & 0xFFFF);
      }
      break;
  case NODE_IDENT: {
      int off = find_local(n->ident.name);
      if (!off) error("undefined variable '%s'", n->ident.name);
      emit("    ldr     w0, [x29, #-%d]", off);
      break;
  }
  case NODE_BINARY:
      gen_arm_expr(n->binary.left);
      emit("    str     w0, [sp, #-16]!");   /* push left */
      gen_arm_expr(n->binary.right);
      emit("    mov     w1, w0");             /* right in w1 */
      emit("    ldr     w0, [sp], #16");      /* pop left into w0 */
      switch (n->binary.op) {
      case TOK_PLUS:  emit("    add     w0, w0, w1"); break;
      case TOK_MINUS: emit("    sub     w0, w0, w1"); break;
      case TOK_STAR:  emit("    mul     w0, w0, w1"); break;
      case TOK_SLASH: emit("    sdiv    w0, w0, w1"); break;
      case TOK_LT:
          emit("    cmp     w0, w1");
          emit("    cset    w0, lt");
          break;
      case TOK_GT:
          emit("    cmp     w0, w1");
          emit("    cset    w0, gt");
          break;
      case TOK_EQ:
          emit("    cmp     w0, w1");
          emit("    cset    w0, eq");
          break;
      case TOK_NEQ:
          emit("    cmp     w0, w1");
          emit("    cset    w0, ne");
          break;
      default: error("unknown binary op");
      }
      break;
  case NODE_CALL: {
      /* Push args in reverse, then pop into w0-w7 */
      for (int i = n->call.arg_count - 1; i >= 0; i--) {
          gen_arm_expr(n->call.args[i]);
          emit("    str     w0, [sp, #-16]!");
      }
      for (int i = 0; i < n->call.arg_count && i < 8; i++)
          emit("    ldr     w%d, [sp], #16", i);
      emit("    bl      %s", sym(n->call.name));
      break;
  }
  default: error("unexpected node in expression");
  }
}

static void gen_arm_stmt(ASTNode *n) {
  switch (n->type) {
  case NODE_RETURN:
      gen_arm_expr(n->ret.expr);
      emit("    mov     sp, x29");
      emit("    ldp     x29, x30, [sp], #16");
      emit("    ret");
      break;
  case NODE_VAR_DECL: {
      int off = add_local(n->var_decl.name);
      if (n->var_decl.init) {
          gen_arm_expr(n->var_decl.init);
          emit("    str     w0, [x29, #-%d]", off);
      }
      break;
  }
  case NODE_ASSIGN: {
      int off = find_local(n->assign.name);
      if (!off) error("undefined variable '%s'", n->assign.name);
      gen_arm_expr(n->assign.expr);
      emit("    str     w0, [x29, #-%d]", off);
      break;
  }
  case NODE_IF: {
      int lelse = new_label(), lend = new_label();
      gen_arm_expr(n->if_stmt.cond);
      emit("    cmp     w0, #0");
      emit("    b.eq    .L%d", n->if_stmt.else_body ? lelse : lend);
      for (int i = 0; i < n->if_stmt.then_body->block.count; i++)
          gen_arm_stmt(n->if_stmt.then_body->block.stmts[i]);
      if (n->if_stmt.else_body) {
          emit("    b       .L%d", lend);
          emit(".L%d:", lelse);
          for (int i = 0; i < n->if_stmt.else_body->block.count; i++)
              gen_arm_stmt(n->if_stmt.else_body->block.stmts[i]);
      }
      emit(".L%d:", lend);
      break;
  }
  case NODE_EXPR_STMT:
      gen_arm_expr(n->expr_stmt.expr);
      break;
  case NODE_BLOCK:
      for (int i = 0; i < n->block.count; i++)
          gen_arm_stmt(n->block.stmts[i]);
      break;
  default: error("unexpected statement type");
  }
}

static void gen_arm_func(ASTNode *fn) {
  local_count = 0;
  frame_size = 0;

  /* Register params as locals */
  for (int i = 0; i < fn->func.param_count; i++)
      add_local(fn->func.params[i]);

  /* Pre-scan for var declarations */
  scan_locals(fn->func.body);
  int total_frame = (frame_size + 15) & ~15;
  local_count = 0; frame_size = 0;
  for (int i = 0; i < fn->func.param_count; i++)
      add_local(fn->func.params[i]);

  emit("");
  emit("    .globl  %s", sym(fn->func.name));
  emit("%s:", sym(fn->func.name));
  emit("    stp     x29, x30, [sp, #-16]!");
  emit("    mov     x29, sp");
  if (total_frame > 0)
      emit("    sub     sp, sp, #%d", total_frame);

  /* Store params from registers to stack */
  for (int i = 0; i < fn->func.param_count && i < 8; i++)
      emit("    str     w%d, [x29, #-%d]", i, locals[i].offset);

  /* Generate body */
  for (int i = 0; i < fn->func.body->block.count; i++)
      gen_arm_stmt(fn->func.body->block.stmts[i]);

  /* Implicit return 0 */
  emit("    mov     w0, #0");
  emit("    mov     sp, x29");
  emit("    ldp     x29, x30, [sp], #16");
  emit("    ret");
}

static void gen_arm(ASTNode *prog) {
  emit("    .text");
  for (int i = 0; i < prog->program.count; i++)
      gen_arm_func(prog->program.funcs[i]);
  if (!is_macos) {
      emit("");
      emit("    .section .note.GNU-stack,"",@progbits");
  }
}

/* ================================================================
* SECTION 10: MAIN
* ================================================================ */

int main(int argc, char **argv) {
  const char *infile = NULL;
  out = stdout;

  for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "-arch") && i + 1 < argc) {
          i++;
          if (!strcmp(argv[i], "x86_64")) arch = ARCH_X86_64;
          else if (!strcmp(argv[i], "arm64")) arch = ARCH_ARM64;
          else { fprintf(stderr, "unknown arch: %s
", argv[i]); return 1; }
      } else if (!strcmp(argv[i], "-macos")) {
          is_macos = 1;
      } else {
          infile = argv[i];
      }
  }

  if (!infile) {
      fprintf(stderr, "Usage: minicc [-arch x86_64|arm64] input.mc
");
      return 1;
  }

  /* Read input file */
  FILE *f = fopen(infile, "r");
  if (!f) { perror(infile); return 1; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc(len + 1);
  if (!buf) { fprintf(stderr, "out of memory
"); return 1; }
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
