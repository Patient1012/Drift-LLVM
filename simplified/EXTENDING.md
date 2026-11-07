# Extending MiniC

This guide shows how to add a feature without getting lost. The important
idea is that every feature usually touches the same few stages:

```
source text
   |
   v
 lexer        turns characters into tokens
   |
   v
 parser       turns tokens into an AST
   |
   v
 semantic     finds locals and stack slots
   |
   v
 codegen      walks the AST and writes assembly
```

If a feature needs new syntax, it starts at the top. If it only changes
behaviour, it may only touch codegen.

## Where each kind of change goes

| Change | Look at |
|---|---|
| New keyword or operator | `TokenType`, `next_token`, `token_type_name` |
| New syntax shape | `NodeType`, `struct ASTNode`, `parse_*` |
| New declaration | `scan_locals`, `add_local`, `prepare_frame` |
| New instruction | `generate_x86_*` and `generate_arm_*` |
| New command-line option | `main` |

## General recipe

1. Decide the exact syntax and behaviour.
2. Add a token if the feature introduces a keyword or operator.
3. Add an AST node or field if the feature has a new shape.
4. Parse it in the parser.
5. If it declares a variable, teach `scan_locals` and `add_local`.
6. Generate code for **both** x86-64 and ARM64.
7. Update the README and add a test program.
8. Compile with warnings and test both backends.

Forgetting step 6 is the most common mistake: one architecture will work
and the other will print "unexpected statement type" or generate bad code.

---

# Example 1: adding `else if`

MiniC already supports ordinary `else`:

```c
if (x < y) {
    return 1;
} else {
    return 0;
}
```

The current AST expects the `else` branch to be a `NODE_BLOCK`. That makes
the smallest implementation easy: when you see `else if`, parse the nested
`if` and wrap it in a block.

## Parser change

In `parse_statement`, find this part of the `TOK_IF` branch:

```c
node->if_statement.else_body = NULL;
if (token.type == TOK_ELSE) {
    next_token();
    expect(TOK_LBRACE);
    node->if_statement.else_body = parse_block();
    expect(TOK_RBRACE);
}
```

Replace it with:

```c
node->if_statement.else_body = NULL;

if (token.type == TOK_ELSE) {
    next_token();

    if (token.type == TOK_IF) {
        /* else if: parse the nested if and put it in a block. */
        ASTNode *block = new_node(NODE_BLOCK);

        block->block.statements = allocate(
            MAX_STATEMENTS * sizeof(ASTNode *));
        block->block.count = 0;
        block->block.statements[block->block.count++] = parse_statement();
        node->if_statement.else_body = block;
    } else {
        expect(TOK_LBRACE);
        node->if_statement.else_body = parse_block();
        expect(TOK_RBRACE);
    }
}
```

No codegen changes are needed, because `else_body` is still a block. The
block simply contains one statement: another `NODE_IF`.

Test it with:

```c
int main() {
    int x = 5;

    if (x < 0) {
        return 1;
    } else if (x == 5) {
        return 2;
    } else {
        return 3;
    }
}
```

This program should exit with code 2.

## Cleaner alternative

A more general AST lets `else_body` be any statement, not only a block.
That requires a helper like this:

```c
static ASTNode *parse_statement_or_block(void) {
    if (token.type == TOK_LBRACE) {
        ASTNode *block;
        next_token();
        block = parse_block();
        expect(TOK_RBRACE);
        return block;
    }

    return parse_statement();
}
```

Then `else_body` can be parsed with `parse_statement_or_block()`. The code
generator must be changed to call `generate_x86_statement(else_body)` and
`generate_arm_statement(else_body)` directly instead of looping over
`else_body->block.statements`.

Start with the block-wrapping version. It is smaller and keeps the AST
contract simple.

## Adding `elif` as a keyword

If you prefer `elif` instead of `else if`:

1. Add `TOK_ELIF` to `TokenType`.
2. Recognise `"elif"` in `next_token`.
3. Add `case TOK_ELIF: return "elif";` to `token_type_name`.
4. In the parser, treat `TOK_ELIF` the same as `TOK_ELSE` followed by
   `TOK_IF`.

This is more work than it looks, because the parser must avoid consuming
the `elif` token twice. `else if` needs no new token at all, so it is the
better first exercise.

---

# Example 2: adding the `%` operator

This is the smallest complete feature: one token, one parser change, and
one instruction in each backend.

## 1. Add the token

In `TokenType`, next to `TOK_SLASH`:

```c
TOK_PLUS,
TOK_MINUS,
TOK_STAR,
TOK_SLASH,
TOK_PERCENT,
```

## 2. Lex it

In the single-character switch in `next_token`:

```c
case '%': token.type = TOK_PERCENT; return;
```

Add a name for error messages in `token_type_name`:

```c
case TOK_PERCENT: return "%";
```

## 3. Parse it

`%` has the same precedence as `*` and `/`, so add it to
`parse_multiplication`:

```c
while (token.type == TOK_STAR ||
       token.type == TOK_SLASH ||
       token.type == TOK_PERCENT) {
```

## 4. Generate x86-64

`idiv` leaves the quotient in `eax` and the remainder in `edx`:

```c
case TOK_PERCENT:
    emit("    cdq");
    emit("    idiv    ecx");
    emit("    mov     eax, edx");
    break;
```

## 5. Generate ARM64

Divide, then subtract `quotient * right` from the original left value:

```c
case TOK_PERCENT:
    emit("    sdiv    w2, w0, w1");
    emit("    msub    w0, w2, w1, w0");
    break;
```

In `msub w0, w2, w1, w0`, the result is:

```
w0 = original_w0 - (w2 * w1)
```

which is the remainder.

## 6. Test

```c
int main() {
    return 17 % 5;
}
```

The exit code should be 2.

---

# Example 3: adding a `while` loop

A loop needs a new AST node because it has a condition and a body.

## 1. Token

Add `TOK_WHILE`, recognise `"while"` in `next_token`, and add it to
`token_type_name`.

## 2. AST

Add `NODE_WHILE` to `NodeType`:

```c
NODE_IF,
NODE_WHILE,
NODE_VAR_DECL,
```

Add its fields to the union:

```c
struct {
    ASTNode *condition;
    ASTNode *body;
} while_statement;
```

## 3. Parser

Add this branch to `parse_statement`:

```c
if (token.type == TOK_WHILE) {
    ASTNode *node;
    next_token();

    node = new_node(NODE_WHILE);
    expect(TOK_LPAREN);
    node->while_statement.condition = parse_expression();
    expect(TOK_RPAREN);

    expect(TOK_LBRACE);
    node->while_statement.body = parse_block();
    expect(TOK_RBRACE);

    return node;
}
```

## 4. Find locals inside the loop

`scan_locals` must look inside the loop body, otherwise variables declared
there get no stack slot:

```c
} else if (node->type == NODE_WHILE) {
    scan_locals(node->while_statement.body);
}
```

## 5. Generate x86-64

The label pattern is: start label, test, exit label, body, jump back.

```c
case NODE_WHILE: {
    int start_label = new_label();
    int end_label = new_label();

    emit(".L%d:", start_label);
    generate_x86_expression(node->while_statement.condition);
    emit("    test    eax, eax");
    emit("    je      .L%d", end_label);

    for (i = 0; i < node->while_statement.body->block.count; i++) {
        generate_x86_statement(
            node->while_statement.body->block.statements[i]);
    }

    emit("    jmp     .L%d", start_label);
    emit(".L%d:", end_label);
    break;
}
```

## 6. Generate ARM64

The same shape with ARM instructions:

```c
case NODE_WHILE: {
    int start_label = new_label();
    int end_label = new_label();

    emit(".L%d:", start_label);
    generate_arm_expression(node->while_statement.condition);
    emit("    cmp     w0, #0");
    emit("    b.eq    .L%d", end_label);

    for (i = 0; i < node->while_statement.body->block.count; i++) {
        generate_arm_statement(
            node->while_statement.body->block.statements[i]);
    }

    emit("    b       .L%d", start_label);
    emit(".L%d:", end_label);
    break;
}
```

Test with:

```c
int main() {
    int total = 0;
    int i = 1;

    while (i < 5) {
        total = total + i;
        i = i + 1;
    }

    return total;
}
```

The exit code should be 10.

---

# Example 4: adding new types

There are two levels of "new type":

1. A new name that behaves exactly like `int` (`bool`, `i32`).
2. A type with a different storage size (`i16`, `char`).

Start with the first one. It teaches the token/parser plumbing without
changing the backends.

## 4a. Add a type alias such as `bool`

### Token

Add `TOK_BOOL` to `TokenType`, recognise `"bool"` in `next_token`, and add
it to `token_type_name`.

### Parser

Right now the parser checks for `TOK_INT` directly in two places:
variable declarations and functions. Replace those checks with a helper:

```c
static int is_type_token(TokenType type) {
    return type == TOK_INT || type == TOK_BOOL;
}
```

Then in `parse_statement`:

```c
if (is_type_token(token.type)) {
    next_token();
    /* ... the rest of variable declaration parsing is unchanged ... */
}
```

In `parse_function`, use the helper for the return type and for each
parameter type:

```c
if (!is_type_token(token.type)) {
    error("expected a type");
}
next_token();
```

Because `bool` is stored and operated on exactly like `int`, the code
generator needs no changes.

### `true` and `false`

Add `TOK_TRUE` and `TOK_FALSE` tokens, recognise `"true"` and `"false"`,
and handle them in `parse_primary`:

```c
if (token.type == TOK_TRUE) {
    ASTNode *node = new_node(NODE_NUMBER);
    node->number.value = 1;
    next_token();
    return node;
}

if (token.type == TOK_FALSE) {
    ASTNode *node = new_node(NODE_NUMBER);
    node->number.value = 0;
    next_token();
    return node;
}
```

This is another good example of a feature that changes the frontend only.

## 4b. Add a real narrow type such as `i16`

First add the token: `TOK_I16` in `TokenType`, recognise `"i16"` in
`next_token`, and add it to `token_type_name`.

This is where you need to decide semantics before writing code:

- Is `i16` signed or unsigned?
- Does arithmetic happen at 32 bits and then narrow on assignment?
- Can you assign an `int` to an `i16` without a cast?
- What happens on overflow?
- What does a function return?

For a first version, keep it simple:

- `i16` is signed.
- Arithmetic is done in 32-bit registers.
- Values are sign-extended when loaded and truncated when stored.
- Implicit conversions are allowed; there is no full type checker yet.
- Every local still gets a 4-byte stack slot, so offsets do not change.
  Only the load/store width changes.

### Type representation

Add a small enum:

```c
typedef enum {
    TYPE_INT,
    TYPE_I16
} Type;
```

Store the type where it is needed:

```c
typedef struct {
    char name[MAX_NAME];
    Type type;
    int offset;
} Local;
```

Add a `Type type` to `var_decl` and a `Type return_type` to `function`.
Functions also need parameter types. The smallest change is an extra array:

```c
Type parameter_types[MAX_PARAMETERS];
```

### Parser helper

```c
static Type parse_type(void) {
    Type type;

    if (token.type == TOK_INT) {
        type = TYPE_INT;
    } else if (token.type == TOK_I16) {
        type = TYPE_I16;
    } else {
        error("expected a type");
        return TYPE_INT;
    }

    next_token();
    return type;
}
```

Use `parse_type()` for variable declarations, function return types, and
parameters.

### Stack slots

Change `add_local` so it also records the type:

```c
static int add_local(const char *name, Type type) {
    if (local_count >= MAX_LOCALS) {
        error("too many local variables");
    }

    frame_size += 4;            /* still one 4-byte slot per local */
    strcpy(locals[local_count].name, name);
    locals[local_count].type = type;
    locals[local_count].offset = frame_size;
    local_count++;
    return frame_size;
}
```

Then update the callers:

```c
/* in scan_locals */
add_local(node->var_decl.name, node->var_decl.type);

/* in prepare_frame */
add_local(function->function.parameters[i],
          function->function.parameter_types[i]);
```

The code generator also needs the type when it loads or stores a variable.
`find_local` currently returns only an offset, so change it to return a
pointer to the whole entry:

```c
static Local *find_local(const char *name) {
    int i;

    for (i = 0; i < local_count; i++) {
        if (strcmp(locals[i].name, name) == 0) {
            return &locals[i];
        }
    }
    return NULL;
}
```

Then a lookup looks like this:

```c
Local *local = find_local(node->identifier.name);
if (!local) {
    error("undefined variable '%s'", node->identifier.name);
}
```

### x86-64 load and store

Instead of always using `DWORD PTR` and `eax`, choose based on the type:

```c
if (local->type == TYPE_I16) {
    emit("    movsx   eax, WORD PTR [rbp-%d]", local->offset);
} else {
    emit("    mov     eax, DWORD PTR [rbp-%d]", local->offset);
}
```

To store the low 16 bits:

```c
emit("    mov     WORD PTR [rbp-%d], ax", offset);
```

`movsx` sign-extends the 16-bit value into the full 32-bit register, so
normal `add`, `sub`, `imul`, and comparisons still work.

### ARM64 load and store

```c
if (local->type == TYPE_I16) {
    emit("    ldrsh   w0, [x29, #-%d]", local->offset);
} else {
    emit("    ldr     w0, [x29, #-%d]", local->offset);
}
```

To store:

```c
emit("    strh    w0, [x29, #-%d]", local->offset);
```

Use `ldrsh` for signed halfwords. If you choose an unsigned type, use
`ldrh` instead.

### Why keep 4-byte slots?

If `i16` uses only 2 bytes, the stack layout needs more care:

- offsets must be 2-byte aligned
- the frame size must still be 16-byte aligned
- two `i16` locals can share one 4-byte slot, or each can get a 2-byte slot

Keeping one 4-byte slot per local avoids all of that complexity. You lose
a little memory, but the feature is much easier to understand and debug.

### Function parameters and returns

For parameters, store the low 16 bits from the argument register. On
x86-64 that means using the 16-bit register name (for example `di` instead
of `edi`), not `ax`:

```c
const char *parameter_registers_16[] =
    {"di", "si", "dx", "cx", "r8w", "r9w"};

emit("    mov     WORD PTR [rbp-%d], %s",
     locals[i].offset, parameter_registers_16[i]);   /* x86-64 */

emit("    strh    w%d, [x29, #-%d]", i, offset);      /* ARM64 */
```

Return values still travel in the full 32-bit register. If the function
returns `i16`, sign-extend the value before `ret`:

```c
emit("    movsx   eax, ax");   /* x86-64 */
emit("    sxth    w0, w0");    /* ARM64 */
```

Without this, the caller may see garbage in the upper bits.

### Type checking is a separate job

The parser only builds the AST. A real type checker would be another pass
that walks it and answers questions such as:

- Are both sides of `+` the same type?
- Is the condition of an `if` an integer?
- Does the number of arguments match the parameter count?
- Does a function call know the return type?

MiniC currently does none of this. If you add a type checker, put it
between parsing and code generation. It should report errors, not emit
assembly.

---

# More feature ideas

Once the examples above make sense, these are natural next steps:

| Feature | What it teaches |
|---|---|
| `for` loops | desugaring into simpler AST nodes |
| `&&` and `||` | short-circuit code generation |
| `!` operator | unary operators and booleans |
| `void` functions | return types and calling conventions |
| `%` operator | adding an operator end to end |
| Global variables | data sections and symbol naming |
| A `-o` output option | driver and file handling |
| An AST printer | debugging and tree traversal |

A good learning exercise is to write a small AST printer:

```c
static void print_ast(ASTNode *node, int depth);
```

It walks the tree and prints indented node names. It makes parser bugs
obvious and requires no code generation changes.

---

# Testing checklist

After a change:

1. Build with warnings treated as errors:

   ```sh
   make clean
   make CFLAGS='-std=c11 -Wall -Wextra -Werror -O2'
   ```

2. Generate and run x86-64 assembly:

   ```sh
   ./minicc test.mc > test.s
   cc test.s -o test
   ./test
   echo $?
   ```

3. Generate ARM64 assembly and at least assemble it:

   ```sh
   ./minicc -arch arm64 test.mc > test_arm.s
   clang --target=aarch64-linux-gnu -c test_arm.s -o test_arm.o
   ```

4. Test the error paths, not only the success path. For example:

   ```c
   int main() { return missing; }
   ```

   should produce a clear "undefined variable" error.

5. Test edge cases: zero, negative numbers, nested `if`, recursion, and
   empty function bodies.

6. Update the README if the language changed.

---

# Common mistakes

- **Forgetting `next_token()`** after accepting a keyword. The parser then
  sees the same token forever.
- **Reading the wrong union member.** After checking `node->type`, only use
  the matching field.
- **Forgetting `scan_locals`.** A new declaration gets no stack slot, so
  its offset is 0 and `find_local` reports "undefined variable".
- **Updating only one backend.** Both x86-64 and ARM64 must handle every new
  node type and operator.
- **Confusing tokens and nodes.** Token types start with `TOK_`; AST types
  start with `NODE_`.
- **Using `token.name` after calling `next_token()`.** The buffer has
  changed. Copy the name first.
- **Assuming `else_body` is a block.** If you generalise it to any
  statement, update the code generators.
- **Mixing storage width and arithmetic width.** For `i16`, load with
  sign extension, calculate at 32 bits, then store only the low 16 bits.
