/*
 * minicc - a tiny compiler for MiniC.
 *
 * MiniC is a small subset of C. This compiler turns MiniC into x86-64 or
 * ARM64 assembly, which can then be assembled and linked with the system
 * toolchain.
 *
 * Supported language features:
 *   - one integer type: int
 *   - arithmetic: + - * /
 *   - comparisons: < > == !=
 *   - local variables and assignment
 *   - if / else
 *   - functions, parameters, calls and recursion
 *
 * Usage:
 *   minicc [-arch x86_64|arm64] [-macos] program.mc
 *
 * The assembly is written to standard output.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * LIMITS
 * ================================================================ */

#define MAX_NAME 64
#define MAX_FUNCTIONS 64
#define MAX_PARAMETERS 8
#define MAX_ARGUMENTS 8
#define MAX_STATEMENTS 128
#define MAX_LOCALS 128
#define STACK_ALIGNMENT 16

/* ================================================================
 * TOKENS
 * ================================================================ */

typedef enum {
    /* keywords */
    TOK_INT,
    TOK_IF,
    TOK_ELSE,
    TOK_RETURN,

    /* literals */
    TOK_IDENT,
    TOK_NUM,

    /* arithmetic */
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,

    /* comparison */
    TOK_LT,
    TOK_GT,
    TOK_EQ,
    TOK_NEQ,

    /* punctuation */
    TOK_ASSIGN,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_SEMI,
    TOK_COMMA,

    TOK_EOF
} TokenType;

typedef struct {
    TokenType type;
    char name[MAX_NAME];
    int value;
    int line;
} Token;

/* ================================================================
 * ABSTRACT SYNTAX TREE
 * ================================================================ */

typedef enum {
    NODE_PROGRAM,
    NODE_FUNCTION,
    NODE_BLOCK,
    NODE_RETURN,
    NODE_IF,
    NODE_VAR_DECL,
    NODE_ASSIGN,
    NODE_BINARY,
    NODE_CALL,
    NODE_NUMBER,
    NODE_IDENTIFIER,
    NODE_EXPRESSION_STATEMENT
} NodeType;

typedef struct ASTNode ASTNode;

struct ASTNode {
    NodeType type;
    int line;

    union {
        struct {
            ASTNode **functions;
            int count;
        } program;

        struct {
            char name[MAX_NAME];
            char parameters[MAX_PARAMETERS][MAX_NAME];
            int parameter_count;
            ASTNode *body;
        } function;

        struct {
            ASTNode **statements;
            int count;
        } block;

        struct {
            ASTNode *expression;
        } return_statement;

        struct {
            ASTNode *condition;
            ASTNode *then_body;
            ASTNode *else_body;
        } if_statement;

        struct {
            char name[MAX_NAME];
            ASTNode *initializer;
        } var_decl;

        struct {
            char name[MAX_NAME];
            ASTNode *expression;
        } assign;

        struct {
            TokenType op;
            ASTNode *left;
            ASTNode *right;
        } binary;

        struct {
            char name[MAX_NAME];
            ASTNode **arguments;
            int argument_count;
        } call;

        struct {
            int value;
        } number;

        struct {
            char name[MAX_NAME];
        } identifier;

        struct {
            ASTNode *expression;
        } expression_statement;
    };
};

/* ================================================================
 * GLOBAL COMPILER STATE
 * ================================================================ */

static const char *source;
static int source_position;
static int current_line = 1;
static Token token;

static int next_label_id;
static int target_is_macos;
static FILE *output;

typedef struct {
    char name[MAX_NAME];
    int offset;
} Local;

static Local locals[MAX_LOCALS];
static int local_count;
static int frame_size;

/* ================================================================
 * ERRORS AND MEMORY
 * ================================================================ */

static void error(const char *format, ...) {
    va_list arguments;

    fprintf(stderr, "minicc:%d: error: ", current_line);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fprintf(stderr, "\n");
    exit(1);
}

static void *allocate(size_t size) {
    void *memory = calloc(1, size);
    if (!memory) {
        fprintf(stderr, "minicc: out of memory\n");
        exit(1);
    }
    return memory;
}

/* ================================================================
 * LEXER
 * ================================================================ */

static void skip_whitespace(void) {
    for (;;) {
        if (source[source_position] == '\n') {
            current_line++;
            source_position++;
        } else if (isspace((unsigned char)source[source_position])) {
            source_position++;
        } else if (source[source_position] == '/' &&
                   source[source_position + 1] == '/') {
            while (source[source_position] &&
                   source[source_position] != '\n') {
                source_position++;
            }
        } else {
            return;
        }
    }
}

static void next_token(void) {
    skip_whitespace();
    token.line = current_line;

    if (source[source_position] == '\0') {
        token.type = TOK_EOF;
        strcpy(token.name, "EOF");
        return;
    }

    if (isdigit((unsigned char)source[source_position])) {
        token.type = TOK_NUM;
        token.value = 0;
        while (isdigit((unsigned char)source[source_position])) {
            token.value = token.value * 10 + (source[source_position] - '0');
            source_position++;
        }
        snprintf(token.name, MAX_NAME, "%d", token.value);
        return;
    }

    if (isalpha((unsigned char)source[source_position]) ||
        source[source_position] == '_') {
        int start = source_position;
        int length;

        while (isalnum((unsigned char)source[source_position]) ||
               source[source_position] == '_') {
            source_position++;
        }

        length = source_position - start;
        if (length >= MAX_NAME) {
            length = MAX_NAME - 1;
        }
        memcpy(token.name, source + start, length);
        token.name[length] = '\0';

        if (strcmp(token.name, "int") == 0) {
            token.type = TOK_INT;
        } else if (strcmp(token.name, "if") == 0) {
            token.type = TOK_IF;
        } else if (strcmp(token.name, "else") == 0) {
            token.type = TOK_ELSE;
        } else if (strcmp(token.name, "return") == 0) {
            token.type = TOK_RETURN;
        } else {
            token.type = TOK_IDENT;
        }
        return;
    }

    if (source[source_position] == '=' &&
        source[source_position + 1] == '=') {
        token.type = TOK_EQ;
        strcpy(token.name, "==");
        source_position += 2;
        return;
    }

    if (source[source_position] == '!' &&
        source[source_position + 1] == '=') {
        token.type = TOK_NEQ;
        strcpy(token.name, "!=");
        source_position += 2;
        return;
    }

    {
        char character = source[source_position++];
        token.name[0] = character;
        token.name[1] = '\0';

        switch (character) {
        case '+': token.type = TOK_PLUS;   return;
        case '-': token.type = TOK_MINUS;  return;
        case '*': token.type = TOK_STAR;   return;
        case '/': token.type = TOK_SLASH;  return;
        case '<': token.type = TOK_LT;     return;
        case '>': token.type = TOK_GT;     return;
        case '=': token.type = TOK_ASSIGN; return;
        case '(': token.type = TOK_LPAREN; return;
        case ')': token.type = TOK_RPAREN; return;
        case '{': token.type = TOK_LBRACE; return;
        case '}': token.type = TOK_RBRACE; return;
        case ';': token.type = TOK_SEMI;   return;
        case ',': token.type = TOK_COMMA;  return;
        default:
            error("unexpected character '%c'", character);
        }
    }
}

static const char *token_type_name(TokenType type) {
    switch (type) {
    case TOK_INT:    return "int";
    case TOK_IF:     return "if";
    case TOK_ELSE:   return "else";
    case TOK_RETURN: return "return";
    case TOK_IDENT:  return "identifier";
    case TOK_NUM:    return "number";
    case TOK_PLUS:   return "+";
    case TOK_MINUS:  return "-";
    case TOK_STAR:   return "*";
    case TOK_SLASH:  return "/";
    case TOK_LT:     return "<";
    case TOK_GT:     return ">";
    case TOK_EQ:     return "==";
    case TOK_NEQ:    return "!=";
    case TOK_ASSIGN: return "=";
    case TOK_LPAREN: return "(";
    case TOK_RPAREN: return ")";
    case TOK_LBRACE: return "{";
    case TOK_RBRACE: return "}";
    case TOK_SEMI:   return ";";
    case TOK_COMMA:  return ",";
    case TOK_EOF:    return "end of file";
    }
    return "unknown token";
}

static void expect(TokenType type) {
    if (token.type != type) {
        error("expected '%s' but found '%s'",
              token_type_name(type), token.name);
    }
    next_token();
}

/* "Rewinding" the lexer lets the parser peek past an identifier. */
typedef struct {
    int position;
    int line;
    Token token;
} LexerState;

static LexerState save_lexer(void) {
    LexerState state;
    state.position = source_position;
    state.line = current_line;
    state.token = token;
    return state;
}

static void restore_lexer(LexerState state) {
    source_position = state.position;
    current_line = state.line;
    token = state.token;
}

/* ================================================================
 * AST CONSTRUCTION
 * ================================================================ */

static ASTNode *new_node(NodeType type) {
    ASTNode *node = allocate(sizeof(ASTNode));
    node->type = type;
    node->line = token.line;
    return node;
}

/* ================================================================
 * PARSER
 *
 * Each function parses one level of the grammar. Lower-precedence
 * functions call higher-precedence ones, which is the usual shape of a
 * recursive-descent parser.
 * ================================================================ */

static ASTNode *parse_expression(void);
static ASTNode *parse_statement(void);

static ASTNode *parse_primary(void) {
    if (token.type == TOK_NUM) {
        ASTNode *node = new_node(NODE_NUMBER);
        node->number.value = token.value;
        next_token();
        return node;
    }

    if (token.type == TOK_IDENT) {
        char name[MAX_NAME];
        strcpy(name, token.name);
        next_token();

        if (token.type == TOK_LPAREN) {
            ASTNode *node;
            next_token();

            node = new_node(NODE_CALL);
            strcpy(node->call.name, name);
            node->call.arguments = allocate(
                MAX_ARGUMENTS * sizeof(ASTNode *));
            node->call.argument_count = 0;

            if (token.type != TOK_RPAREN) {
                node->call.arguments[node->call.argument_count++] =
                    parse_expression();

                while (token.type == TOK_COMMA) {
                    next_token();
                    if (node->call.argument_count >= MAX_ARGUMENTS) {
                        error("too many arguments");
                    }
                    node->call.arguments[node->call.argument_count++] =
                        parse_expression();
                }
            }

            expect(TOK_RPAREN);
            return node;
        }

        {
            ASTNode *node = new_node(NODE_IDENTIFIER);
            strcpy(node->identifier.name, name);
            return node;
        }
    }

    if (token.type == TOK_LPAREN) {
        ASTNode *node;
        next_token();
        node = parse_expression();
        expect(TOK_RPAREN);
        return node;
    }

    error("unexpected '%s' in expression", token.name);
    return NULL;
}

/* Unary minus is represented as "0 - value". This keeps the AST small. */
static ASTNode *parse_unary(void) {
    if (token.type == TOK_MINUS) {
        ASTNode *node;
        next_token();

        node = new_node(NODE_BINARY);
        node->binary.op = TOK_MINUS;
        node->binary.left = new_node(NODE_NUMBER);
        node->binary.left->number.value = 0;
        node->binary.right = parse_primary();
        return node;
    }

    return parse_primary();
}

static ASTNode *parse_multiplication(void) {
    ASTNode *left = parse_unary();

    while (token.type == TOK_STAR || token.type == TOK_SLASH) {
        ASTNode *node = new_node(NODE_BINARY);
        node->binary.op = token.type;
        next_token();
        node->binary.left = left;
        node->binary.right = parse_unary();
        left = node;
    }

    return left;
}

static ASTNode *parse_addition(void) {
    ASTNode *left = parse_multiplication();

    while (token.type == TOK_PLUS || token.type == TOK_MINUS) {
        ASTNode *node = new_node(NODE_BINARY);
        node->binary.op = token.type;
        next_token();
        node->binary.left = left;
        node->binary.right = parse_multiplication();
        left = node;
    }

    return left;
}

/* Comparisons have the lowest precedence in MiniC. */
static ASTNode *parse_expression(void) {
    ASTNode *left = parse_addition();

    while (token.type == TOK_LT || token.type == TOK_GT ||
           token.type == TOK_EQ || token.type == TOK_NEQ) {
        ASTNode *node = new_node(NODE_BINARY);
        node->binary.op = token.type;
        next_token();
        node->binary.left = left;
        node->binary.right = parse_addition();
        left = node;
    }

    return left;
}

static ASTNode *parse_block(void) {
    ASTNode *block = new_node(NODE_BLOCK);
    block->block.statements = allocate(
        MAX_STATEMENTS * sizeof(ASTNode *));
    block->block.count = 0;

    while (token.type != TOK_RBRACE && token.type != TOK_EOF) {
        if (block->block.count >= MAX_STATEMENTS) {
            error("too many statements in block");
        }
        block->block.statements[block->block.count++] = parse_statement();
    }

    return block;
}

static ASTNode *parse_statement(void) {
    if (token.type == TOK_RETURN) {
        ASTNode *node;
        next_token();

        node = new_node(NODE_RETURN);
        node->return_statement.expression = parse_expression();
        expect(TOK_SEMI);
        return node;
    }

    if (token.type == TOK_IF) {
        ASTNode *node;
        next_token();

        node = new_node(NODE_IF);
        expect(TOK_LPAREN);
        node->if_statement.condition = parse_expression();
        expect(TOK_RPAREN);

        expect(TOK_LBRACE);
        node->if_statement.then_body = parse_block();
        expect(TOK_RBRACE);

        node->if_statement.else_body = NULL;
        if (token.type == TOK_ELSE) {
            next_token();
            expect(TOK_LBRACE);
            node->if_statement.else_body = parse_block();
            expect(TOK_RBRACE);
        }

        return node;
    }

    if (token.type == TOK_INT) {
        ASTNode *node;
        next_token();

        node = new_node(NODE_VAR_DECL);
        if (token.type != TOK_IDENT) {
            error("expected a variable name after 'int'");
        }
        strcpy(node->var_decl.name, token.name);
        next_token();

        node->var_decl.initializer = NULL;
        if (token.type == TOK_ASSIGN) {
            next_token();
            node->var_decl.initializer = parse_expression();
        }

        expect(TOK_SEMI);
        return node;
    }

    /*
     * A statement that starts with an identifier is either an assignment
     * or an expression statement. Peek one token ahead to tell them apart.
     */
    if (token.type == TOK_IDENT) {
        LexerState saved = save_lexer();
        char name[MAX_NAME];

        strcpy(name, token.name);
        next_token();

        if (token.type == TOK_ASSIGN) {
            ASTNode *node;
            next_token();

            node = new_node(NODE_ASSIGN);
            strcpy(node->assign.name, name);
            node->assign.expression = parse_expression();
            expect(TOK_SEMI);
            return node;
        }

        restore_lexer(saved);
    }

    {
        ASTNode *node = new_node(NODE_EXPRESSION_STATEMENT);
        node->expression_statement.expression = parse_expression();
        expect(TOK_SEMI);
        return node;
    }
}

static ASTNode *parse_function(void) {
    ASTNode *node;

    expect(TOK_INT);

    node = new_node(NODE_FUNCTION);
    if (token.type != TOK_IDENT) {
        error("expected a function name");
    }
    strcpy(node->function.name, token.name);
    next_token();

    expect(TOK_LPAREN);
    node->function.parameter_count = 0;

    if (token.type != TOK_RPAREN) {
        if (node->function.parameter_count >= MAX_PARAMETERS) {
            error("too many parameters");
        }
        expect(TOK_INT);
        if (token.type != TOK_IDENT) {
            error("expected a parameter name");
        }
        strcpy(node->function.parameters[node->function.parameter_count++],
               token.name);
        next_token();

        while (token.type == TOK_COMMA) {
            next_token();
            if (node->function.parameter_count >= MAX_PARAMETERS) {
                error("too many parameters");
            }
            expect(TOK_INT);
            if (token.type != TOK_IDENT) {
                error("expected a parameter name");
            }
            strcpy(
                node->function.parameters[node->function.parameter_count++],
                token.name);
            next_token();
        }
    }

    expect(TOK_RPAREN);
    expect(TOK_LBRACE);
    node->function.body = parse_block();
    expect(TOK_RBRACE);
    return node;
}

static ASTNode *parse_program(void) {
    ASTNode *program = new_node(NODE_PROGRAM);
    program->program.functions = allocate(
        MAX_FUNCTIONS * sizeof(ASTNode *));
    program->program.count = 0;

    while (token.type != TOK_EOF) {
        if (program->program.count >= MAX_FUNCTIONS) {
            error("too many functions");
        }
        program->program.functions[program->program.count++] =
            parse_function();
    }

    return program;
}

/* ================================================================
 * SHARED CODE-GENERATION HELPERS
 * ================================================================ */

static void emit(const char *format, ...) {
    va_list arguments;

    va_start(arguments, format);
    vfprintf(output, format, arguments);
    va_end(arguments);
    fputc('\n', output);
}

static int new_label(void) {
    return next_label_id++;
}

/* macOS expects an underscore in front of C symbol names. */
static const char *platform_symbol(const char *name) {
    static char buffer[128];

    if (!target_is_macos) {
        return name;
    }

    snprintf(buffer, sizeof(buffer), "_%s", name);
    return buffer;
}

static int find_local(const char *name) {
    int i;

    for (i = 0; i < local_count; i++) {
        if (strcmp(locals[i].name, name) == 0) {
            return locals[i].offset;
        }
    }
    return 0;
}

static int add_local(const char *name) {
    if (local_count >= MAX_LOCALS) {
        error("too many local variables");
    }

    frame_size += 4;
    strcpy(locals[local_count].name, name);
    locals[local_count].offset = frame_size;
    local_count++;
    return frame_size;
}

static void scan_locals(ASTNode *node) {
    int i;

    if (!node) {
        return;
    }

    if (node->type == NODE_VAR_DECL) {
        add_local(node->var_decl.name);
    } else if (node->type == NODE_IF) {
        scan_locals(node->if_statement.then_body);
        scan_locals(node->if_statement.else_body);
    } else if (node->type == NODE_BLOCK) {
        for (i = 0; i < node->block.count; i++) {
            scan_locals(node->block.statements[i]);
        }
    }
}

/*
 * Work out how much stack space a function needs.
 *
 * We scan the body once to reserve space for every local, then reset the
 * symbol table so the code generator can build it again in the same order.
 */
static int prepare_frame(ASTNode *function) {
    int i;
    int aligned_size;

    local_count = 0;
    frame_size = 0;

    for (i = 0; i < function->function.parameter_count; i++) {
        add_local(function->function.parameters[i]);
    }

    scan_locals(function->function.body);
    aligned_size =
        (frame_size + STACK_ALIGNMENT - 1) & ~(STACK_ALIGNMENT - 1);

    local_count = 0;
    frame_size = 0;

    for (i = 0; i < function->function.parameter_count; i++) {
        add_local(function->function.parameters[i]);
    }

    return aligned_size;
}

/* ================================================================
 * x86-64 CODE GENERATOR
 * ================================================================ */

static void generate_x86_expression(ASTNode *node);
static void generate_x86_statement(ASTNode *node);

static void generate_x86_expression(ASTNode *node) {
    switch (node->type) {
    case NODE_NUMBER:
        emit("    mov     eax, %d", node->number.value);
        break;

    case NODE_IDENTIFIER: {
        int offset = find_local(node->identifier.name);
        if (!offset) {
            error("undefined variable '%s'", node->identifier.name);
        }
        emit("    mov     eax, DWORD PTR [rbp-%d]", offset);
        break;
    }

    case NODE_BINARY:
        generate_x86_expression(node->binary.left);
        emit("    push    rax");
        generate_x86_expression(node->binary.right);
        emit("    mov     ecx, eax");
        emit("    pop     rax");

        switch (node->binary.op) {
        case TOK_PLUS:
            emit("    add     eax, ecx");
            break;
        case TOK_MINUS:
            emit("    sub     eax, ecx");
            break;
        case TOK_STAR:
            emit("    imul    eax, ecx");
            break;
        case TOK_SLASH:
            emit("    cdq");
            emit("    idiv    ecx");
            break;
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
        default:
            error("unknown binary operator");
        }
        break;

    case NODE_CALL: {
        const char *argument_registers[] =
            {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        int i;

        /* Push arguments in reverse, then pop them into ABI registers. */
        for (i = node->call.argument_count - 1; i >= 0; i--) {
            generate_x86_expression(node->call.arguments[i]);
            emit("    push    rax");
        }

        for (i = 0; i < node->call.argument_count && i < 6; i++) {
            emit("    pop     %s", argument_registers[i]);
        }

        emit("    xor     eax, eax");
        emit("    call    %s", platform_symbol(node->call.name));
        break;
    }

    default:
        error("unexpected node in expression");
    }
}

static void generate_x86_statement(ASTNode *node) {
    int i;

    switch (node->type) {
    case NODE_RETURN:
        generate_x86_expression(node->return_statement.expression);
        emit("    leave");
        emit("    ret");
        break;

    case NODE_VAR_DECL: {
        int offset = add_local(node->var_decl.name);
        if (node->var_decl.initializer) {
            generate_x86_expression(node->var_decl.initializer);
            emit("    mov     DWORD PTR [rbp-%d], eax", offset);
        }
        break;
    }

    case NODE_ASSIGN: {
        int offset = find_local(node->assign.name);
        if (!offset) {
            error("undefined variable '%s'", node->assign.name);
        }
        generate_x86_expression(node->assign.expression);
        emit("    mov     DWORD PTR [rbp-%d], eax", offset);
        break;
    }

    case NODE_IF: {
        int else_label = new_label();
        int end_label = new_label();

        generate_x86_expression(node->if_statement.condition);
        emit("    test    eax, eax");
        emit("    je      .L%d",
             node->if_statement.else_body ? else_label : end_label);

        for (i = 0; i < node->if_statement.then_body->block.count; i++) {
            generate_x86_statement(
                node->if_statement.then_body->block.statements[i]);
        }

        if (node->if_statement.else_body) {
            emit("    jmp     .L%d", end_label);
            emit(".L%d:", else_label);

            for (i = 0; i < node->if_statement.else_body->block.count; i++) {
                generate_x86_statement(
                    node->if_statement.else_body->block.statements[i]);
            }
        }

        emit(".L%d:", end_label);
        break;
    }

    case NODE_EXPRESSION_STATEMENT:
        generate_x86_expression(node->expression_statement.expression);
        break;

    case NODE_BLOCK:
        for (i = 0; i < node->block.count; i++) {
            generate_x86_statement(node->block.statements[i]);
        }
        break;

    default:
        error("unexpected statement type");
    }
}

static void generate_x86_function(ASTNode *function) {
    const char *parameter_registers[] =
        {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
    int total_frame = prepare_frame(function);
    int i;

    emit("");
    emit("    .globl  %s", platform_symbol(function->function.name));
    if (!target_is_macos) {
        emit("    .type   %s, @function",
             platform_symbol(function->function.name));
    }
    emit("%s:", platform_symbol(function->function.name));
    emit("    push    rbp");
    emit("    mov     rbp, rsp");

    if (total_frame > 0) {
        emit("    sub     rsp, %d", total_frame);
    }

    for (i = 0; i < function->function.parameter_count && i < 6; i++) {
        emit("    mov     DWORD PTR [rbp-%d], %s",
             locals[i].offset, parameter_registers[i]);
    }

    for (i = 0; i < function->function.body->block.count; i++) {
        generate_x86_statement(function->function.body->block.statements[i]);
    }

    /* Functions without a return statement return 0. */
    emit("    xor     eax, eax");
    emit("    leave");
    emit("    ret");
}

static void generate_x86_program(ASTNode *program) {
    int i;

    emit("    .intel_syntax noprefix");
    emit("    .text");

    for (i = 0; i < program->program.count; i++) {
        generate_x86_function(program->program.functions[i]);
    }

    if (!target_is_macos) {
        emit("");
        emit("    .section .note.GNU-stack,\"\",@progbits");
    }
}

/* ================================================================
 * ARM64 CODE GENERATOR (AAPCS64)
 * ================================================================ */

static void generate_arm_expression(ASTNode *node);
static void generate_arm_statement(ASTNode *node);

static void generate_arm_expression(ASTNode *node) {
    switch (node->type) {
    case NODE_NUMBER:
        if (node->number.value >= 0 && node->number.value <= 65535) {
            emit("    mov     w0, #%d", node->number.value);
        } else {
            emit("    mov     w0, #%d", node->number.value & 0xFFFF);
            if (node->number.value >> 16) {
                emit("    movk    w0, #%d, lsl #16",
                     (node->number.value >> 16) & 0xFFFF);
            }
        }
        break;

    case NODE_IDENTIFIER: {
        int offset = find_local(node->identifier.name);
        if (!offset) {
            error("undefined variable '%s'", node->identifier.name);
        }
        emit("    ldr     w0, [x29, #-%d]", offset);
        break;
    }

    case NODE_BINARY:
        generate_arm_expression(node->binary.left);
        emit("    str     w0, [sp, #-16]!");
        generate_arm_expression(node->binary.right);
        emit("    mov     w1, w0");
        emit("    ldr     w0, [sp], #16");

        switch (node->binary.op) {
        case TOK_PLUS:
            emit("    add     w0, w0, w1");
            break;
        case TOK_MINUS:
            emit("    sub     w0, w0, w1");
            break;
        case TOK_STAR:
            emit("    mul     w0, w0, w1");
            break;
        case TOK_SLASH:
            emit("    sdiv    w0, w0, w1");
            break;
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
        default:
            error("unknown binary operator");
        }
        break;

    case NODE_CALL: {
        int i;

        for (i = node->call.argument_count - 1; i >= 0; i--) {
            generate_arm_expression(node->call.arguments[i]);
            emit("    str     w0, [sp, #-16]!");
        }

        for (i = 0; i < node->call.argument_count && i < 8; i++) {
            emit("    ldr     w%d, [sp], #16", i);
        }

        emit("    bl      %s", platform_symbol(node->call.name));
        break;
    }

    default:
        error("unexpected node in expression");
    }
}

static void generate_arm_statement(ASTNode *node) {
    int i;

    switch (node->type) {
    case NODE_RETURN:
        generate_arm_expression(node->return_statement.expression);
        emit("    mov     sp, x29");
        emit("    ldp     x29, x30, [sp], #16");
        emit("    ret");
        break;

    case NODE_VAR_DECL: {
        int offset = add_local(node->var_decl.name);
        if (node->var_decl.initializer) {
            generate_arm_expression(node->var_decl.initializer);
            emit("    str     w0, [x29, #-%d]", offset);
        }
        break;
    }

    case NODE_ASSIGN: {
        int offset = find_local(node->assign.name);
        if (!offset) {
            error("undefined variable '%s'", node->assign.name);
        }
        generate_arm_expression(node->assign.expression);
        emit("    str     w0, [x29, #-%d]", offset);
        break;
    }

    case NODE_IF: {
        int else_label = new_label();
        int end_label = new_label();

        generate_arm_expression(node->if_statement.condition);
        emit("    cmp     w0, #0");
        emit("    b.eq    .L%d",
             node->if_statement.else_body ? else_label : end_label);

        for (i = 0; i < node->if_statement.then_body->block.count; i++) {
            generate_arm_statement(
                node->if_statement.then_body->block.statements[i]);
        }

        if (node->if_statement.else_body) {
            emit("    b       .L%d", end_label);
            emit(".L%d:", else_label);

            for (i = 0; i < node->if_statement.else_body->block.count; i++) {
                generate_arm_statement(
                    node->if_statement.else_body->block.statements[i]);
            }
        }

        emit(".L%d:", end_label);
        break;
    }

    case NODE_EXPRESSION_STATEMENT:
        generate_arm_expression(node->expression_statement.expression);
        break;

    case NODE_BLOCK:
        for (i = 0; i < node->block.count; i++) {
            generate_arm_statement(node->block.statements[i]);
        }
        break;

    default:
        error("unexpected statement type");
    }
}

static void generate_arm_function(ASTNode *function) {
    int total_frame = prepare_frame(function);
    int i;

    emit("");
    emit("    .globl  %s", platform_symbol(function->function.name));
    emit("%s:", platform_symbol(function->function.name));
    emit("    stp     x29, x30, [sp, #-16]!");
    emit("    mov     x29, sp");

    if (total_frame > 0) {
        emit("    sub     sp, sp, #%d", total_frame);
    }

    for (i = 0; i < function->function.parameter_count && i < 8; i++) {
        emit("    str     w%d, [x29, #-%d]", i, locals[i].offset);
    }

    for (i = 0; i < function->function.body->block.count; i++) {
        generate_arm_statement(function->function.body->block.statements[i]);
    }

    /* Functions without a return statement return 0. */
    emit("    mov     w0, #0");
    emit("    mov     sp, x29");
    emit("    ldp     x29, x30, [sp], #16");
    emit("    ret");
}

static void generate_arm_program(ASTNode *program) {
    int i;

    emit("    .text");

    for (i = 0; i < program->program.count; i++) {
        generate_arm_function(program->program.functions[i]);
    }

    if (!target_is_macos) {
        emit("");
        emit("    .section .note.GNU-stack,\"\",@progbits");
    }
}

/* ================================================================
 * DRIVER
 * ================================================================ */

typedef enum {
    TARGET_X86_64,
    TARGET_ARM64
} Target;

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    long length;
    char *buffer;

    if (!file) {
        perror(path);
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (length < 0) {
        fprintf(stderr, "minicc: could not read '%s'\n", path);
        fclose(file);
        exit(1);
    }

    buffer = allocate((size_t)length + 1);
    if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "minicc: could not read '%s'\n", path);
        fclose(file);
        exit(1);
    }

    buffer[length] = '\0';
    fclose(file);
    return buffer;
}

static void print_usage(void) {
    fprintf(stderr,
            "Usage: minicc [-arch x86_64|arm64] [-macos] input.mc\n");
}

int main(int argc, char **argv) {
    const char *input_path = NULL;
    Target target = TARGET_X86_64;
    char *source_buffer;
    ASTNode *program;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-arch") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "x86_64") == 0) {
                target = TARGET_X86_64;
            } else if (strcmp(argv[i], "arm64") == 0) {
                target = TARGET_ARM64;
            } else {
                fprintf(stderr, "minicc: unknown architecture '%s'\n",
                        argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-macos") == 0) {
            target_is_macos = 1;
        } else {
            input_path = argv[i];
        }
    }

    if (!input_path) {
        print_usage();
        return 1;
    }

    source_buffer = read_file(input_path);
    source = source_buffer;
    source_position = 0;
    output = stdout;

    next_token();
    program = parse_program();

    if (target == TARGET_X86_64) {
        generate_x86_program(program);
    } else {
        generate_arm_program(program);
    }

    free(source_buffer);
    return 0;
}
