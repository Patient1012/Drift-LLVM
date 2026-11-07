# Simplified MiniC compiler

A single-file version of the MiniC compiler, written to be easy to read.

The original split-up source in `../src/` is left untouched. This copy keeps
the same language and generated assembly, but uses one file and clearer names.

## Build

```sh
make
```

## Use

```sh
./minicc program.mc > program.s
cc program.s -o program
./program
```

Choose a target with `-arch`:

```sh
./minicc -arch x86_64 program.mc
./minicc -arch arm64 program.mc
```

Add `-macos` when assembling/linking on macOS (it prefixes symbols with `_`).

## Language

```c
int main() {
    int x = 5;
    int y = 7;

    if (x < y) {
        return x + y;
    }

    return 0;
}
```

Supported: `int`, `+ - * /`, `< > == !=`, variables, assignment, `if`/`else`,
functions, parameters, calls and recursion. There are no loops, pointers,
strings, or other types.

## Reading order

The file is arranged in the same order as the compiler pipeline:

1. Tokens
2. Abstract syntax tree
3. Global state
4. Lexer
5. Parser
6. Shared code-generation helpers
7. x86-64 generator
8. ARM64 generator
9. Driver (`main`)

## Extending the compiler

See [EXTENDING.md](EXTENDING.md) for a step-by-step guide to adding
features, with worked examples for `else if`, a new operator, a `while`
loop, and new types.
