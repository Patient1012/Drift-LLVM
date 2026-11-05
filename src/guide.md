niC: A Complete Guide to a Small C Compiler

## 1. Introduction

MiniC is a small compiler written in C. It takes source code written in a simplified version of C and translates it into assembly language that can be assembled and linked into an executable program.

The implementation is approximately 850 lines long and is designed to demonstrate the fundamental components of a compiler without introducing the complexity found in GCC or Clang.

The compiler supports:

- Integer variables.
- Integer arithmetic.
- Comparison operators.
- Variable declarations and assignments.
- Functions and parameters.
- Function calls and recursion.
- Conditional statements (`if` and `else`).
- Return statements.
- Nested expressions.

It supports two output architectures:

- x86-64.
- ARM64.

It is important to understand that MiniC does not use LLVM. It generates assembly directly. This makes it a useful starting point for understanding what a compiler actually does.

The purpose of this document is to explain the compiler's architecture and implementation so that you can understand it, modify it and eventually extend it into a more capable programming language.

---

# 2. Understanding the compilation process

Before looking at individual functions, it is important to understand the overall process.

A compiler translates one programming language into another representation.

For MiniC, the process looks like this:

    fibonacci.mc
         |
         v
    Source Code
         |
         v
    Lexer
         |
         v
    Tokens
         |
         v
    Parser
         |
         v
    Abstract Syntax Tree (AST)
         |
         v
    Semantic Analysis
         |
         v
    Code Generation
         |
         v
    Assembly (.s)
         |
         v
    GCC Assembler and Linker
         |
         v
    Executable

Each stage has a separate responsibility.

### 2.1 Source code

The input is a file containing MiniC code.

For example:

    int add(int a, int b) {
        return a + b;
    }

    int main() {
        return add(10, 20);
    }

At this point, the compiler sees nothing more than characters in a file.

It does not inherently understand that `int` represents a type, that `add` represents a function, or that `a + b` represents an arithmetic operation.

Those meanings are established by subsequent stages.

### 2.2 Lexer

The lexer reads the characters and groups them into meaningful units called tokens.

For example:

    int add(int a, int b) {
        return a + b;
    }

Becomes something conceptually similar to:

    TOK_INT
    TOK_IDENT("add")
    TOK_LPAREN
    TOK_INT
    TOK_IDENT("a")
    TOK_COMMA
    TOK_INT
    TOK_IDENT("b")
    TOK_RPAREN
    TOK_LBRACE
    TOK_RETURN
    TOK_IDENT("a")
    TOK_PLUS
    TOK_IDENT("b")
    TOK_SEMI
    TOK_RBRACE

The parser doesn't need to inspect individual characters anymore. It can work with tokens.

### 2.3 Parser

The parser takes the token stream and determines how the tokens fit together according to the language grammar.

It builds an Abstract Syntax Tree.

For example:

    return a + b;

Becomes conceptually:

    RETURN
       |
       BINARY (+)
       /       \
    IDENT(a)  IDENT(b)

The AST represents the structure of the program rather than its original textual appearance.

### 2.4 Semantic analysis

The parser can determine whether a statement is grammatically valid, but that doesn't necessarily mean the program makes sense.

Consider:

    int main() {
        return unknown_variable;
    }

This is syntactically valid, but `unknown_variable` has never been declared.

Semantic analysis checks things like:

- Whether variables exist.
- Whether functions exist.
- Whether function calls have the correct number of arguments.
- Whether identifiers are used correctly.
- Whether types are compatible.

MiniC has a relatively simple implementation because it only supports one type.

### 2.5 Code generation

The code generator walks through the AST and emits assembly instructions.

For example:

    return 10 + 20;

Could become:

    mov eax, 10
    push rax
    mov eax, 20
    mov ecx, eax
    pop rax
    add eax, ecx
    leave
    ret

The exact instructions depend on the target architecture.

### 2.6 Assembling and linking

MiniC produces assembly, not an executable.

GCC can take that assembly and turn it into an executable:

    ./minicc program.mc > program.s
    gcc program.s -o program
    ./program

The assembler converts assembly instructions into machine code.

The linker then resolves symbols and combines the required executable components.

---

# 3. Understanding the C implementation

The compiler is organised into several logical sections, even though everything lives in one source file.

The main components are:

1. Token definitions.
2. AST definitions.
3. Global compiler state.
4. Error handling.
5. Lexer.
6. Parser.
7. Function and variable registration.
8. Semantic information.
9. x86-64 code generation.
10. ARM64 code generation.
11. Main entry point.

Understanding these components individually makes the source much easier to navigate.

---

# 4. Token definitions

The first important part of the compiler is its token system.

MiniC defines its tokens using an enum:

    typedef enum {
        TOK_INT,
        TOK_IF,
        TOK_ELSE,
        TOK_RETURN,

        TOK_IDENT,
        TOK_NUM,

        TOK_PLUS,
        TOK_MINUS,
        TOK_STAR,
        TOK_SLASH,

        TOK_LT,
        TOK_GT,
        TOK_EQ,
        TOK_NEQ,

        TOK_ASSIGN,

        TOK_LPAREN,
        TOK_RPAREN,
        TOK_LBRACE,
        TOK_RBRACE,

        TOK_SEMI,
        TOK_COMMA,

        TOK_EOF
    } TokenType;

An enum allows the compiler to represent different token types using named integer constants.

For example:

    TOK_PLUS

Represents the `+` operator.

Meanwhile:

    TOK_IDENT

Represents an identifier, such as a variable or function name.

### 4.1 The Token structure

The compiler also defines a structure:

    typedef struct {
        TokenType type;
        char name[64];
        int value;
        int line;
    } Token;

This structure stores information about an individual token.

Each field has a purpose.

**type**

Identifies what kind of token it is.

For example, `TOK_INT`, `TOK_PLUS` or `TOK_IDENT`.

**name**

Stores the textual representation of the token.

For example, an identifier might have the name `"variable"`.

**value**

Stores the numeric value of an integer literal.

For example, the token representing `123` has a value of `123`.

**line**

Stores the source line where the token was found.

This is useful for reporting errors.

### 4.2 Why separate token types and token values?

Consider:

    int first = 10;
    int second = 20;

The lexer doesn't need a separate token type for every possible variable name or number.

Instead, it can use:

    TOK_IDENT

for both `first` and `second`.

Likewise, it uses:

    TOK_NUM

for both `10` and `20`.

The actual value is stored separately.

This makes the token system considerably simpler.

---

# 5. The lexer

The lexer is responsible for turning source characters into tokens.

MiniC uses a handwritten lexer rather than a lexer generator such as Flex.

Its main functions are:

- `skip_ws()`
- `next_token()`

The lexer also uses several global variables:

    static const char *src;
    static int pos;
    static int cur_line;
    static Token tok;

### 5.1 Source pointer

`src` points to the source code.

For example:

    int main() {
        return 10;
    }

The compiler reads this source into memory and assigns its address to `src`.

### 5.2 Position

`pos` represents the current character position in the source.

Imagine the source is:

    int x;

The lexer advances through it:

    i n t   x ;

The position changes whenever characters are consumed.

This means the lexer does not need to repeatedly search the source from the beginning.

### 5.3 Current token

`tok` stores the token currently being processed.

Instead of creating a large array containing every token, MiniC maintains a current token and advances it using `next_token()`.

This is a relatively simple approach for a small compiler.

### 5.4 Skipping whitespace

The function:

    skip_ws()

moves past whitespace and comments.

Whitespace includes:

- Spaces.
- Tabs.
- Newlines.

The lexer also supports single-line comments beginning with:

    //

For example:

    int main() {
        // This is a comment
        return 10;
    }

Comments are ignored by the compiler.

The line counter is incremented whenever a newline is encountered.

### 5.5 Reading numbers

The lexer checks whether the current character is a digit:

    if (isdigit((unsigned char)src[pos]))

If it is, the lexer starts constructing an integer value.

The important operation is:

    tok.value = tok.value * 10 + (src[pos++] - '0');

This converts individual decimal characters into an integer.

For example, reading `123`:

First digit:

    0 * 10 + 1 = 1

Second digit:

    1 * 10 + 2 = 12

Third digit:

    12 * 10 + 3 = 123

The lexer then stores the result in `tok.value`.

### 5.6 Reading identifiers

Identifiers begin with a letter or underscore.

Examples:

    variable
    my_function
    _internal
    counter123

The lexer continues consuming characters while they are letters, digits or underscores.

It then checks whether the identifier is a reserved keyword.

For example:

    int
    if
    else
    return

These receive dedicated token types.

Anything else becomes `TOK_IDENT`.

This is an important distinction: the lexer doesn't know whether an identifier represents a variable or function. That is determined later.

### 5.7 Recognising operators

Some operators consist of multiple characters.

For example:

    =
    ==

These must be distinguished.

The lexer checks for two-character operators before processing single-character operators.

Otherwise, `==` could accidentally become two assignment tokens.

The same applies to:

    !=

Single-character operators are handled with a switch statement.

### 5.8 Advancing through the source

The function `next_token()` is one of the most important functions in the entire compiler.

It effectively means:

"Find the next meaningful unit in the source and make it the current token."

Almost every parser function depends on this operation.

When reading the source, pay particular attention to where `next_token()` is called. It controls how the parser advances through the program.

---

# 6. Abstract Syntax Trees

The AST is arguably the most important data structure in the compiler.

It represents the program in a form that is easy to process.

MiniC defines its node types using another enum:

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

Each node represents a different programming construct.

| Node | Meaning |
|---|---|
| NODE_PROGRAM | Entire program |
| NODE_FUNC | Function definition |
| NODE_BLOCK | Group of statements |
| NODE_RETURN | Return statement |
| NODE_IF | Conditional statement |
| NODE_VAR_DECL | Variable declaration |
| NODE_ASSIGN | Variable assignment |
| NODE_BINARY | Binary operation |
| NODE_CALL | Function call |
| NODE_NUM | Integer literal |
| NODE_IDENT | Identifier |
| NODE_EXPR_STMT | Expression used as a statement |

### 6.1 The ASTNode structure

The main structure is:

    typedef struct ASTNode ASTNode;

    struct ASTNode {
        NodeType type;
        int line;

        union {
            /* Different node data */
        };
    };

The important part is the union.

A union allows several different data structures to occupy the same memory region.

Only one member is intended to be active at a time.

The `type` field tells the compiler which member should be accessed.

For example:

If:

    node->type == NODE_NUM

Then the compiler should access:

    node->num.value

If:

    node->type == NODE_BINARY

Then it should access:

    node->binary.left
    node->binary.right
    node->binary.op

This avoids needing a completely separate structure for every AST node.

### 6.2 Program node

The program node stores the functions:

    struct {
        ASTNode **funcs;
        int count;
    } program;

`funcs` is an array of pointers to AST nodes.

Each pointer represents a function definition.

`count` records how many functions are present.

### 6.3 Function node

A function contains:

- Its name.
- Its parameters.
- Its parameter count.
- Its body.

Conceptually:

    int add(int a, int b) {
        return a + b;
    }

Becomes:

    FUNCTION
      name: add
      parameters:
        a
        b
      body:
        BLOCK
          RETURN
            BINARY(+)
              IDENT(a)
              IDENT(b)

### 6.4 Block node

A block represents a collection of statements.

For example:

    {
        int x = 10;
        return x;
    }

The block stores an array of statement pointers.

This allows the compiler to process statements one at a time.

### 6.5 Binary node

A binary node represents an operation involving two operands.

For example:

    a + b

Contains:

    operator: TOK_PLUS
    left: IDENT(a)
    right: IDENT(b)

The same node structure can represent:

    a - b
    a * b
    a / b
    a < b
    a == b

The operator field determines which operation is performed.

### 6.6 Why ASTs are useful

Without an AST, the code generator would need to understand the original source text directly.

That would make implementing features considerably harder.

With an AST, the compiler can process the same expression regardless of how it was formatted.

For example:

    return 10 + 20;

And:

    return
        10
        +
        20;

Can produce essentially the same AST.

The AST separates the meaning of a program from its textual representation.

---

# 7. Memory allocation for AST nodes

MiniC uses:

    calloc()

to allocate AST nodes.

Its node creation function is:

    static ASTNode *new_node(NodeType t) {
        ASTNode *n = calloc(1, sizeof(ASTNode));

        if (!n) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }

        n->type = t;
        n->line = tok.line;

        return n;
    }

There are several important things happening here.

First, memory is allocated for one AST node.

Second, `calloc()` initialises the allocated memory to zero.

Third, the node's type is assigned.

Finally, the node is returned to the caller.

Using zero-initialised memory is useful because pointers that have not yet been assigned start as NULL.

For a larger compiler, you might eventually replace individual allocations with an arena allocator.

An arena allocator allows the compiler to allocate many objects from one larger memory region and release them together.

However, individual `calloc()` calls are perfectly understandable for a small educational compiler.

---

# 8. The parser

The parser is responsible for understanding the structure of the language.

MiniC uses recursive descent parsing.

This means that individual parser functions correspond to individual grammar rules.

For example:

- `parse_expr()` parses expressions.
- `parse_stmt()` parses statements.
- `parse_primary()` parses basic expressions.
- `parse_block_body()` parses blocks.
- `parse_program()` parses the entire program.

The parser repeatedly examines the current token and decides what to do next.

### 8.1 Recursive descent

Consider:

    int main() {
        return 10 + 20;
    }

The parser recognises:

1. A function declaration.
2. A function name.
3. A parameter list.
4. A function body.
5. A return statement.
6. An arithmetic expression.

Each part is handled by an appropriate parsing function.

These functions call one another.

That is where the term recursive descent comes from.

### 8.2 Parsing primary expressions

The function:

    parse_primary()

handles the simplest expressions.

These include:

- Integer literals.
- Identifiers.
- Function calls.
- Parenthesised expressions.

For example:

    123

Creates a `NODE_NUM`.

Meanwhile:

    variable

Creates a `NODE_IDENT`.

A function call such as:

    calculate(10, 20)

Creates a `NODE_CALL`.

Parenthesised expressions are handled by recursively calling `parse_expr()`.

For example:

    (10 + 20)

The parser processes the expression inside the parentheses before continuing.

### 8.3 Parsing unary expressions

The function:

    parse_unary()

handles unary minus.

For example:

    -10

MiniC represents this as:

    0 - 10

This is a shortcut.

It means the compiler doesn't need a separate unary-negation node.

It can reuse the existing binary subtraction node.

This works for simple integer expressions, although a more complete compiler would usually represent unary operations separately.

### 8.4 Operator precedence

Operator precedence determines which operations happen first.

Consider:

    10 + 5 * 2

Multiplication must happen before addition.

The result should be:

    10 + (5 * 2)

Not:

    (10 + 5) * 2

MiniC handles this by splitting expression parsing into multiple functions.

The hierarchy is:

    parse_expr()
        |
        v
    parse_add()
        |
        v
    parse_mul()
        |
        v
    parse_unary()
        |
        v
    parse_primary()

The lower-level parsing functions handle operators with higher precedence.

#### Multiplication and division

`parse_mul()` handles:

    *
    /

It first parses a unary expression and then repeatedly checks for multiplication or division.

#### Addition and subtraction

`parse_add()` handles:

    +
    -

It calls `parse_mul()` to process each operand.

This ensures multiplication happens before addition.

#### Comparisons

`parse_expr()` handles:

    <
    >
    ==
    !=

It calls `parse_add()` for its operands.

This places comparisons below arithmetic operations.

### 8.5 Why loops are used in expression parsing

Consider:

    10 + 20 + 30 + 40

The parser starts with the first operand.

It then repeatedly checks whether another addition or subtraction operator exists.

Each time it finds one, it creates a new binary node.

The resulting AST is effectively:

    ADD
      |
      +---- ADD
      |       |
      |       +---- ADD
      |       |       |
      |       |       +---- 10
      |       |       +---- 20
      |       |
      |       +---- 30
      |
      +---- 40

This produces left-associative arithmetic.

### 8.6 Parsing statements

Statements are handled by:

    parse_stmt()

The current token determines what kind of statement is being parsed.

For example:

| Current token | Action |
|---|---|
| TOK_RETURN | Parse return statement |
| TOK_IF | Parse conditional |
| TOK_INT | Parse variable declaration |
| TOK_IDENT | Determine assignment or expression |

The parser uses the token type to select the correct parsing function.

This is an extremely common design in small compilers.

### 8.7 Parsing blocks

A block is a group of statements enclosed in braces.

For example:

    {
        int x = 10;
        int y = 20;
        return x + y;
    }

`parse_block_body()` repeatedly calls `parse_stmt()` until it reaches a closing brace.

The resulting AST contains a list of statement nodes.

This is how the compiler handles multiple statements within a function.

### 8.8 Parsing function declarations

A function definition contains:

- A return type.
- A name.
- A parameter list.
- A body.

For example:

    int calculate(int x, int y) {
        return x * y;
    }

The parser creates a `NODE_FUNC` and stores all the relevant information.

The function body is represented by another AST node.

This allows the code generator to process functions independently.

---

# 9. Semantic analysis and symbol tables

Parsing determines whether a program follows the grammar.

Semantic analysis determines whether the program is meaningful.

MiniC has a relatively lightweight implementation.

It uses two important structures:

1. A local variable table.
2. A function registry.

## 9.1 Local variables

The compiler defines:

    typedef struct {
        char name[64];
        int offset;
    } Local;

This structure associates a variable name with an offset in the function's stack frame.

For example:

    int main() {
        int x = 10;
        int y = 20;
        return x + y;
    }

Could produce a table like:

| Name | Stack offset |
|---|---:|
| x | -4 |
| y | -8 |

The stack frame is where the function stores local data.

### 9.2 Stack frame offsets

On x86-64, the compiler commonly uses the base pointer register `rbp` to access local variables.

For example:

    mov DWORD PTR [rbp-4], eax

This stores a value at an address four bytes below the base pointer.

Similarly:

    mov eax, DWORD PTR [rbp-4]

Loads that value.

The offset is obtained from the local variable table.

### 9.3 Adding local variables

The function:

    add_local()

allocates another four-byte slot.

Conceptually:

    frame_size += 4;

It then stores the variable's name and offset in the local table.

This implementation assumes every variable is an integer.

That is why each variable occupies four bytes.

### 9.4 Function registry

MiniC also maintains a function registry:

    typedef struct {
        char name[64];
        int params;
    } FuncReg;

This stores information about declared functions.

For example:

    int add(int a, int b);

Would be represented by something like:

| Function | Parameters |
|---|---:|
| add | 2 |

The registry allows the compiler to identify functions and check their argument counts.

### 9.5 Why register functions before generating code?

Imagine:

    int main() {
        return calculate(10);
    }

    int calculate(int x) {
        return x * 2;
    }

The function `calculate` appears after `main`.

If the compiler only knew about functions it had already encountered, it might not recognise this call.

Registering functions before code generation avoids that problem.

This is a simple example of a two-pass approach:

**Pass one:** Discover and register functions.

**Pass two:** Generate code for the functions.

### 9.6 Limitations of the symbol table

MiniC's local variable table is deliberately simple.

It does not provide a complete implementation of lexical scopes.

For example, a more advanced language might allow:

    int main() {
        int x = 10;

        {
            int x = 20;
        }

        return x;
    }

A proper scope system needs to distinguish between the two variables.

MiniC's flat local table does not provide that behaviour reliably.

A more capable implementation would use a stack of scopes.

Each scope would contain its own symbol table.

When entering a block, the compiler pushes a new scope.

When leaving a block, it removes that scope.

This is a natural feature to add later.

---

# 10. Code generation

Code generation converts AST nodes into assembly instructions.

This is where the compiler becomes architecture-specific.

MiniC contains separate code generators for:

- x86-64.
- ARM64.

The important idea is that both backends process the same AST.

The AST does not need to know which architecture will eventually execute the program.

## 10.1 Expression generation

The code generator recursively visits expression nodes.

For example:

    10 + 20

The generator:

1. Generates the left expression.
2. Saves its result.
3. Generates the right expression.
4. Restores the left result.
5. Emits the addition instruction.

The x86-64 backend uses `eax` as its primary expression result register.

The ARM64 backend uses `w0`.

This gives the compiler a simple rule:

**After generating an expression, its result is available in the designated result register.**

## 10.2 Generating integer literals

For:

    10

The x86-64 backend emits:

    mov eax, 10

This places the integer value into `eax`.

For ARM64, the equivalent is:

    mov w0, #10

## 10.3 Generating binary operations

Consider:

    10 + 20

The compiler uses a push/pop strategy.

First:

    mov eax, 10
    push rax

The left operand is saved.

Then:

    mov eax, 20
    mov ecx, eax

The right operand is placed in `ecx`.

Next:

    pop rax

The left operand is restored.

Finally:

    add eax, ecx

The result is now in `eax`.

The same general pattern is used for subtraction and multiplication.

### 10.4 Division

Integer division is more complicated on x86-64.

MiniC uses:

    cdq
    idiv ecx

`cdq` prepares the dividend by sign-extending `eax` into the `edx:eax` register pair.

`idiv` performs signed integer division.

The quotient is placed in `eax`.

The remainder is placed in `edx`.

ARM64 uses:

    sdiv w0, w0, w1

The ARM64 instruction directly performs signed division.

## 10.5 Comparisons

Consider:

    10 < 20

On x86-64:

    cmp eax, ecx
    setl al
    movzx eax, al

`cmp` compares the two values.

`setl` sets the low byte to one if the first value is less than the second.

`movzx` extends the result to the full register.

The final result is either:

    0

or:

    1

This is how comparison expressions become integer-like boolean results.

## 10.6 Conditional statements

Consider:

    if (x > 10) {
        return 1;
    } else {
        return 0;
    }

The code generator evaluates the condition.

It then checks the result and emits a conditional jump.

Conceptually:

    evaluate condition
    compare result with zero
    jump to else label if false

    generate then block
    jump to end label

    else label:
    generate else block

    end label:

Labels are identifiers used by assembly instructions to mark destinations.

MiniC uses a global label counter to generate unique labels.

For example:

    .L1
    .L2
    .L3

This prevents different conditional statements from accidentally using the same label.

## 10.7 Function calls

Consider:

    add(10, 20)

A function call requires the compiler to:

1. Evaluate the arguments.
2. Put arguments into the correct registers.
3. Execute the call instruction.
4. Retrieve the return value.

On x86-64 Linux, the System V ABI passes the first six integer arguments using:

| Argument | Register |
|---|---|
| 1 | RDI |
| 2 | RSI |
| 3 | RDX |
| 4 | RCX |
| 5 | R8 |
| 6 | R9 |

The generated code eventually uses:

    call add

The return value is placed in `eax`.

On ARM64, the first eight integer arguments are passed in:

    x0 - x7

The return value is placed in `w0` for a 32-bit integer.

## 10.8 Function prologues

Every function needs a stack frame.

The x86-64 function prologue looks like:

    push rbp
    mov rbp, rsp
    sub rsp, 16

These instructions:

1. Save the previous base pointer.
2. Establish a new base pointer.
3. Allocate stack space.

The function can then access its local variables.

The ARM64 backend uses a different mechanism involving `x29` and `x30`.

## 10.9 Function epilogues

When a function finishes, it must restore the stack frame.

On x86-64:

    leave
    ret

`leave` restores the previous stack frame.

`ret` returns execution to the calling function.

On ARM64:

    mov sp, x29
    ldp x29, x30, [sp], #16
    ret

The registers and stack pointer are restored before returning.

## 10.10 Why separate backends?

The AST represents the language, not the processor.

For example:

    return a + b;

Means the same thing on x86-64 and ARM64.

However, the assembly instructions differ.

Separating the backends allows the compiler to support multiple architectures without changing the lexer or parser.

This is also one of the main architectural ideas behind LLVM.

---

# 11. The main function

The main function is the entry point of the compiler itself.

It handles:

- Command-line arguments.
- Input file selection.
- Architecture selection.
- Source file loading.
- Initialising the lexer.
- Parsing the program.
- Registering functions.
- Starting code generation.

The general flow is:

    int main(int argc, char **argv) {
        // Parse command-line arguments

        // Open source file

        // Read source into memory

        // Initialise lexer

        // Parse program

        // Register functions

        // Generate assembly

        // Release source memory

        return 0;
    }

### 11.1 Reading the source file

MiniC opens the input file with:

    fopen()

It determines the file size, allocates enough memory, and reads the contents.

The source is then stored in the global `src` pointer.

This makes the source available to the lexer.

### 11.2 Starting compilation

The compiler sets:

    src = buf;
    pos = 0;

It then calls:

    next_token();

This initialises the current token.

The parser can now begin.

### 11.3 Generating output

After parsing and registering functions, the compiler selects a backend.

Conceptually:

    if (arch == ARCH_X86_64)
        gen_x86(prog);
    else
        gen_arm(prog);

This is where the same AST is translated into different assembly languages.

---

# 12. Understanding what MiniC does not implement

MiniC is deliberately small.

It is important not to mistake its simplicity for a complete implementation of C.

Its limitations are useful because they show exactly where you can add features.

| Feature | Current support |
|---|---|
| Integer type | Yes |
| Floating-point types | No |
| Strings | No |
| Characters | No |
| Boolean type | No dedicated type |
| Variables | Yes |
| Local scopes | Limited |
| Global variables | No |
| Functions | Yes |
| Recursion | Yes |
| Function prototypes | Limited |
| Arrays | No |
| Structs | No |
| Pointers | No |
| While loops | No |
| For loops | No |
| Break/continue | No |
| Logical operators | No |
| Bitwise operators | No |
| Preprocessor | No |
| LLVM | No |

There are also implementation limitations worth knowing:

- Fixed-size arrays are used for several compiler structures.
- Identifiers have a fixed maximum length.
- The local variable table is simplistic.
- There is no complete type-checking system.
- There is no intermediate representation.
- There are no optimisation passes.
- Register allocation is not implemented.
- The backend has limited calling convention handling.
- Error recovery is minimal.

These limitations should be treated as learning opportunities rather than reasons to immediately rewrite everything.

---

# 13. How to extend MiniC

The best way to learn from this compiler is to add features gradually.

A feature usually requires changes to several compiler stages.

For example, adding a new operator might require:

1. A new token.
2. Lexer support.
3. Parser support.
4. An AST representation, if the existing node types are insufficient.
5. Code generation.
6. Tests.

This is an important pattern to understand.

## 13.1 Adding a while loop

A while loop would look like:

    while (x < 10) {
        x = x + 1;
    }

You would need to:

**Lexer**

Add:

    TOK_WHILE

**AST**

Add:

    NODE_WHILE

The node should contain:

- A condition.
- A loop body.

**Parser**

Create a function such as:

    parse_while()

It should parse the condition and body.

**Code generator**

Generate:

    loop_start:

    evaluate condition

    jump to loop_end if false

    generate loop body

    jump to loop_start

    loop_end:

This is a good first extension because it introduces control flow and labels.

## 13.2 Adding else-if

MiniC currently supports:

    if (...) {
    } else {
    }

You could extend it to:

    if (...) {
    } else if (...) {
    } else {
    }

There are two possible approaches.

One is to represent `else if` as another nested `NODE_IF`.

The other is to create a separate conditional-chain representation.

For a small compiler, nested conditional nodes are usually sufficient.

## 13.3 Adding additional integer types

Currently, MiniC only supports signed 32-bit integers.

You could add:

    i8
    i16
    i32
    i64

This requires a proper type representation.

For example:

    typedef enum {
        TYPE_I8,
        TYPE_I16,
        TYPE_I32,
        TYPE_I64
    } TypeKind;

Then AST declarations need to store their type.

The semantic analysis must determine whether operations are valid for the selected types.

The backend must generate appropriate instructions and memory accesses.

This is a considerably larger change than adding a simple operator.

## 13.4 Adding structs

A struct could look like:

    struct Player {
        int health;
        int score;
    };

The compiler would need to understand:

- Struct declarations.
- Member declarations.
- Struct types.
- Member access.
- Memory layout.
- Member offsets.

For example:

    player.health

Would require the compiler to find the offset of `health` within the structure and generate the appropriate memory access.

This is a good way to learn how compilers represent complex data types.

## 13.5 Adding pointers

Pointers require a more substantial extension.

You would need to understand:

- Address-of operations.
- Dereferencing.
- Pointer types.
- Pointer arithmetic.
- Memory addresses.
- Type compatibility.

For example:

    int x = 10;
    int *ptr = &x;

The compiler needs to distinguish between the value stored in `x` and the address where `x` lives.

This is one of the areas where the original MiniC architecture starts becoming restrictive.

---

# 14. Moving towards LLVM

Although MiniC currently generates assembly directly, its architecture provides a useful starting point for understanding LLVM.

The important distinction is between the frontend and backend.

The frontend is responsible for understanding the language.

It includes:

- Lexer.
- Parser.
- AST.
- Semantic analysis.

The backend is responsible for translating the program into executable code.

MiniC currently has two backends:

- x86-64 assembly generation.
- ARM64 assembly generation.

LLVM provides a different approach.

Instead of generating architecture-specific assembly directly, the compiler generates LLVM Intermediate Representation (LLVM IR).

The architecture becomes:

    Source Code
         |
         v
       Lexer
         |
         v
       Parser
         |
         v
        AST
         |
         v
    Semantic Analysis
         |
         v
      LLVM IR
         |
         v
    LLVM Optimisation
         |
         v
    LLVM Backend
         |
         v
    Machine Code

This is useful because the frontend no longer needs to know how to generate instructions for every processor.

### 14.1 What is LLVM IR?

LLVM IR is an intermediate representation used by LLVM.

For example, a simple addition might be represented as:

    define i32 @add(i32 %a, i32 %b) {
    entry:
        %result = add i32 %a, %b
        ret i32 %result
    }

This describes the operation without directly specifying x86-64 or ARM64 instructions.

LLVM can then optimise and translate it into machine code.

### 14.2 What would need changing?

If you eventually wanted to replace MiniC's assembly backends with LLVM, you could retain much of the frontend.

The main change would be replacing:

    gen_x86()
    gen_arm()

With something like:

    gen_llvm()

The new backend would walk the existing AST and construct LLVM IR.

You would also need to introduce LLVM initialisation, module management, target configuration and output generation.

This is one reason it is useful to understand the AST before attempting LLVM integration.

### 14.3 Should LLVM be added immediately?

Not necessarily.

Understanding the existing code generator first will make LLVM considerably easier to understand.

Otherwise, you risk learning two complicated systems simultaneously:

1. How a compiler works.
2. How LLVM works.

You can introduce LLVM once you understand how expressions, variables, functions and control flow are represented.

---

# 15. Recommended way to study the source

Do not try to understand all 850 lines at once.

Read the source in this order:

### First: Understand the data structures

Find:

- `TokenType`
- `Token`
- `NodeType`
- `ASTNode`
- `Local`
- `FuncReg`

Understand what information the compiler stores.

### Second: Understand the lexer

Read:

- `skip_ws()`
- `next_token()`
- `expect()`

Try following a simple source file character by character.

### Third: Understand the parser

Read:

- `parse_primary()`
- `parse_unary()`
- `parse_mul()`
- `parse_add()`
- `parse_expr()`
- `parse_stmt()`
- `parse_block_body()`
- `parse_program()`

Pay particular attention to how each function creates AST nodes.

### Fourth: Understand semantic analysis

Read:

- `add_local()`
- `scan_locals()`
- `register_funcs()`

Understand how identifiers become associated with storage locations and functions.

### Fifth: Understand code generation

Read the x86-64 backend first.

It is useful to understand one architecture completely before studying the second.

Focus on:

- Expression generation.
- Statement generation.
- Variable access.
- Function calls.
- Stack frames.
- Labels.
- Return handling.

Then compare the ARM64 implementation.

### Sixth: Understand main

Finally, read `main()`.

By this point, the entire compilation pipeline should make sense.

---

# 16. Suggested development roadmap

Here is a sensible progression for turning MiniC into a more capable compiler.

| Stage | Feature | Main concepts |
|---|---|---|
| 1 | Improve errors | Diagnostics and source locations |
| 2 | Better symbol tables | Names and scopes |
| 3 | While loops | Control flow and labels |
| 4 | Logical operators | Expression parsing |
| 5 | Boolean type | Type systems |
| 6 | More integer types | Type representation |
| 7 | Floating-point types | Numeric code generation |
| 8 | Arrays | Memory layout and indexing |
| 9 | Structs | Compound types and member offsets |
| 10 | Pointers | Addresses and indirection |
| 11 | Intermediate representation | Compiler architecture |
| 12 | LLVM backend | LLVM integration |
| 13 | Optimisation | Compiler transformations |

You do not need to follow this exact order, but it provides a progression from relatively simple language features to more advanced compiler architecture.

---

# 17. Final thoughts

MiniC is valuable because it demonstrates the core of a compiler without hiding everything behind frameworks and libraries.

The most important concepts to understand are:

1. **Tokens:** How source characters become meaningful units.
2. **AST:** How the program becomes a structured representation.
3. **Parsing:** How the compiler understands the language grammar.
4. **Semantic analysis:** How identifiers, types and functions are resolved.
5. **Code generation:** How the program becomes executable instructions.

Once you understand these five concepts, you have the foundations needed to build a much larger compiler.

The biggest recommendation is to avoid rewriting the entire compiler before you understand it.

Make small changes, follow how information travels between stages, and test every new feature.

The objective isn't to preserve MiniC forever. It is to use a small compiler as a foundation for learning how to build your own.