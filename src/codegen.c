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
      int32_t off = find_local(n->ident.name);
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
      for (int32_t i = n->call.arg_count - 1; i >= 0; i--) {
          gen_x86_expr(n->call.args[i]);
          emit("    push    rax");
      }
      /* Pop int32_to ABI registers */
      for (int32_t i = 0; i < n->call.arg_count && i < 6; i++)
          emit("    pop     %s", (i < 4) ? (const char*[]){"rdi","rsi","rdx","rcx"}[i] : (i==4?"r8":"r9"));
      /* Note: System V ABI requires 16-byte stack alignment at calls.
         Pushing int32_termediate expr results violates this. Works in minicc but unsafe for external libs. */
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
      int32_t off = add_local(n->var_decl.name);
      if (n->var_decl.init) {
          gen_x86_expr(n->var_decl.init);
          emit("    mov     DWORD PTR [rbp-%d], eax", off);
      }
      break;
  }
  case NODE_ASSIGN: {
      int32_t off = find_local(n->assign.name);
      if (!off) error("undefined variable '%s'", n->assign.name);
      gen_x86_expr(n->assign.expr);
      emit("    mov     DWORD PTR [rbp-%d], eax", off);
      break;
  }
  case NODE_IF: {
      int32_t lelse = new_label(), lend = new_label();
      gen_x86_expr(n->if_stmt.cond);
      emit("    test    eax, eax");
      emit("    je      .L%d", n->if_stmt.else_body ? lelse : lend);
      for (int32_t i = 0; i < n->if_stmt.then_body->block.count; i++)
          gen_x86_stmt(n->if_stmt.then_body->block.stmts[i]);
      if (n->if_stmt.else_body) {
          emit("    jmp     .L%d", lend);
          emit(".L%d:", lelse);
          for (int32_t i = 0; i < n->if_stmt.else_body->block.count; i++)
              gen_x86_stmt(n->if_stmt.else_body->block.stmts[i]);
      }
      emit(".L%d:", lend);
      break;
  }
  case NODE_EXPR_STMT:
      gen_x86_expr(n->expr_stmt.expr);
      break;
  case NODE_BLOCK:
      for (int32_t i = 0; i < n->block.count; i++)
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
  for (int32_t i = 0; i < fn->func.param_count; i++)
      add_local(fn->func.params[i]);

  /* Pre-scan body for variable declarations to calculate frame size */
  scan_locals(fn->func.body);
  int32_t total_frame = (frame_size + 15) & ~15; /* align to 16 */
  /* Reset locals — they'll be re-added during codegen */
  local_count = 0; frame_size = 0;
  for (int32_t i = 0; i < fn->func.param_count; i++)
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
  for (int32_t i = 0; i < fn->func.param_count && i < 6; i++)
      emit("    mov     DWORD PTR [rbp-%d], %s", locals[i].offset, regs[i]);

  /* Generate body */
  for (int32_t i = 0; i < fn->func.body->block.count; i++)
      gen_x86_stmt(fn->func.body->block.stmts[i]);

  /* Implicit return 0 */
  emit("    xor     eax, eax");
  emit("    leave");
  emit("    ret");
}

static void gen_x86(ASTNode *prog) {
  emit("    .intel_syntax noprefix");
  emit("    .text");
  for (int32_t i = 0; i < prog->program.count; i++)
      gen_x86_func(prog->program.funcs[i]);
  if (!is_macos) {
      emit("");
      emit("    .section .note.GNU-stack,"","@progbits");
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
      int32_t off = find_local(n->ident.name);
      if (!off) error("undefined variable '%s'", n->ident.name);
      emit("    ldr     w0, [x29, #-%d]", off);
      break;
  }
  case NODE_BINARY:
      gen_arm_expr(n->binary.left);
      emit("    str     w0, [sp, #-16]!");   /* push left */
      gen_arm_expr(n->binary.right);
      emit("    mov     w1, w0");             /* right in w1 */
      emit("    ldr     w0, [sp], #16");      /* pop left int32_to w0 */
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
      /* Push args in reverse, then pop int32_to w0-w7 */
      for (int32_t i = n->call.arg_count - 1; i >= 0; i--) {
          gen_arm_expr(n->call.args[i]);
          emit("    str     w0, [sp, #-16]!");
      }
      for (int32_t i = 0; i < n->call.arg_count && i < 8; i++)
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
      int32_t off = add_local(n->var_decl.name);
      if (n->var_decl.init) {
          gen_arm_expr(n->var_decl.init);
          emit("    str     w0, [x29, #-%d]", off);
      }
      break;
  }
  case NODE_ASSIGN: {
      int32_t off = find_local(n->assign.name);
      if (!off) error("undefined variable '%s'", n->assign.name);
      gen_arm_expr(n->assign.expr);
      emit("    str     w0, [x29, #-%d]", off);
      break;
  }
  case NODE_IF: {
      int32_t lelse = new_label(), lend = new_label();
      gen_arm_expr(n->if_stmt.cond);
      emit("    cmp     w0, #0");
      emit("    b.eq    .L%d", n->if_stmt.else_body ? lelse : lend);
      for (int32_t i = 0; i < n->if_stmt.then_body->block.count; i++)
          gen_arm_stmt(n->if_stmt.then_body->block.stmts[i]);
      if (n->if_stmt.else_body) {
          emit("    b       .L%d", lend);
          emit(".L%d:", lelse);
          for (int32_t i = 0; i < n->if_stmt.else_body->block.count; i++)
              gen_arm_stmt(n->if_stmt.else_body->block.stmts[i]);
      }
      emit(".L%d:", lend);
      break;
  }
  case NODE_EXPR_STMT:
      gen_arm_expr(n->expr_stmt.expr);
      break;
  case NODE_BLOCK:
      for (int32_t i = 0; i < n->block.count; i++)
          gen_arm_stmt(n->block.stmts[i]);
      break;
  default: error("unexpected statement type");
  }
}

static void gen_arm_func(ASTNode *fn) {
  local_count = 0;
  frame_size = 0;

  /* Register params as locals */
  for (int32_t i = 0; i < fn->func.param_count; i++)
      add_local(fn->func.params[i]);

  /* Pre-scan for var declarations */
  scan_locals(fn->func.body);
  int32_t total_frame = (frame_size + 15) & ~15;
  local_count = 0; frame_size = 0;
  for (int32_t i = 0; i < fn->func.param_count; i++)
      add_local(fn->func.params[i]);

  emit("");
  emit("    .globl  %s", sym(fn->func.name));
  emit("%s:", sym(fn->func.name));
  emit("    stp     x29, x30, [sp, #-16]!");
  emit("    mov     x29, sp");
  if (total_frame > 0)
      emit("    sub     sp, sp, #%d", total_frame);

  /* Store params from registers to stack */
  for (int32_t i = 0; i < fn->func.param_count && i < 8; i++)
      emit("    str     w%d, [x29, #-%d]", i, locals[i].offset);

  /* Generate body */
  for (int32_t i = 0; i < fn->func.body->block.count; i++)
      gen_arm_stmt(fn->func.body->block.stmts[i]);

  /* Implicit return 0 */
  emit("    mov     w0, #0");
  emit("    mov     sp, x29");
  emit("    ldp     x29, x30, [sp], #16");
  emit("    ret");
}

static void gen_arm(ASTNode *prog) {
  emit("    .text");
  for (int32_t i = 0; i < prog->program.count; i++)
      gen_arm_func(prog->program.funcs[i]);
  if (!is_macos) {
      emit("");
      emit("    .section .note.GNU-stack,"",@progbits");
  }
}
