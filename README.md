# Drift-LLVM

Drift-LLVM is a native compiler for the Drift language. It reuses the existing
`Drift-Interpreter` lexer, parser, and AST, performs a compiler-specific semantic pass,
emits textual LLVM IR, and uses the host LLVM/clang toolchain to verify the module, produce an
object file, and link a native executable.

This is a proof of concept, not a complete implementation of the Drift specification. The first
end-to-end target is:

```drift
i32 main()
{
    return 42;
}
```

That program has been compiled to a real native executable, run, and verified to exit with code
`42`. The repository also contains examples for arithmetic, control flow, recursion, booleans,
floating-point values, and integer widths.

## Status

Implemented:

- Native compilation for a documented subset of Drift.
- Reuse of the Drift-Interpreter frontend through a project reference.
- Compiler-specific semantic analysis with source-located diagnostics.
- Textual LLVM IR generation, verification, object emission, and native linking.
- A CLI with `build` and `run` commands.
- Unit, semantic, code-generation, CLI, native-executable, and interpreter-comparison tests.

Not implemented:

- `println`, `print`, `read`, or `readln`
- Strings (`str`, `str16`) and string literals
- Arrays, `switch`, and `for`
- Global variables and top-level statements
- Nested functions, closures, and function overloading
- Logical `&&` and `||` (the frontend does not currently parse them)
- Explicit casts or general implicit conversions
- A standard library, external function declarations, or multi-file programs
- Debug information, target selection, cross-compilation, or direct LLVM API integration

See [Supported subset](#supported-subset) and [Unsupported features](#unsupported-features) for
details.

## Requirements

- .NET SDK 10.0 or newer.
- LLVM/clang available on the current machine.
- clang must support textual LLVM IR with opaque pointers (LLVM 15 or newer).
- The `Drift-Interpreter` repository must be a sibling directory of this repository:

  ```text
  Projects/
  ├── Drift-Interpreter/
  └── Drift-LLVM/
  ```

  The project reference is:

  ```xml
  <ProjectReference Include="..\Drift-Interpreter\src\DriftInterpreter.csproj" />
  ```

clang is found in this order:

1. `--clang <path>`
2. The `DRIFT_CLANG` environment variable
3. `clang` on `PATH`

The compiler does not contain hardcoded platform or toolchain paths.

## Build

From the `Drift-LLVM` directory:

```bash
dotnet build Drift.LLVM.sln
```

The CLI executable is built at `bin/Debug/net10.0/drift-llvm`. You can also use `dotnet run`:

```bash
dotnet run --project Drift.LLVM.csproj -- build examples/return42.drf -o return42
```

## Quick start

Compile and run the minimal example:

```bash
dotnet run --project Drift.LLVM.csproj -- build examples/return42.drf -o return42
./return42
echo $?
```

Expected output:

```text
Built /absolute/path/to/return42
42
```

`run` compiles into a temporary directory and executes the result immediately:

```bash
dotnet run --project Drift.LLVM.csproj -- run examples/functions.drf
echo $?
```

Expected exit code: `120`.

## CLI

```text
Usage: drift-llvm <command> [options]

Commands:
  build <file.drf>   Compile a Drift program to a native executable.
  run <file.drf>     Compile and run a Drift program.
  help               Show this help.
```

### `build`

```text
Usage: drift-llvm build <file.drf> [options]

Options:
  -o, --output <path>        Output executable path.
  -O, --opt-level <0-3>      LLVM optimization level (default: 2).
      --emit-llvm <path>     Keep the generated LLVM IR.
      --emit-object <path>   Keep the generated object file.
      --clang <path>         Explicit clang executable.
  -h, --help                 Show this help.
```

If `--output` is omitted, the executable is written to the current directory using the source
file's base name, with `.exe` added on Windows.

### `run`

```text
Usage: drift-llvm run <file.drf> [options] [-- <program args>]

Options:
  -O, --opt-level <0-3>      LLVM optimization level (default: 2).
      --emit-llvm <path>     Keep the generated LLVM IR.
      --emit-object <path>   Keep the generated object file.
      --clang <path>         Explicit clang executable.
  -h, --help                 Show this help.
```

`run` does not accept `--output`. The Drift entry point currently has no parameters, so program
arguments are forwarded to the executable but are not visible to Drift code.

## Examples

All examples are in `examples/` and are part of the verified supported subset. The expected
native exit code is shown for each:

| Example | Demonstrates | Exit code |
| --- | --- | --- |
| `return42.drf` | Minimal entry point | `42` |
| `arithmetic.drf` | Variables, arithmetic, precedence | `43` |
| `control_flow.drf` | `while`, `if`, `elif`, `else` | `20` |
| `functions.drf` | Parameters, return values, recursion | `120` |
| `booleans.drf` | `bool`, `!`, comparison | `7` |
| `floats.drf` | `f64`, floating-point remainder | `1` |
| `types.drf` | `const`, signed/unsigned and wide integers | `9` |

Build one directly:

```bash
dotnet run --project Drift.LLVM.csproj -- build examples/control_flow.drf -o control_flow
./control_flow
echo $?
```

## Supported subset

### Program shape

A source file contains only function declarations. Exactly one case-insensitive `main` function
is required. The entry point must:

- have no parameters; and
- return `void`, `bool`, `char`, or an integer type.

`f16`, `f32`, `f64`, unsigned floats, and `str` are rejected as entry-point return types.

### Types

- Signed integers: `i8`, `i16`, `i32`, `i64`
- Unsigned integers: `ui8`, `ui16`, `ui32`, `ui64`
- Floating point: `f16`, `f32`, `f64`, `uf16`, `uf32`, `uf64`
- `bool`
- `char`
- `void` (function return type only)

`str` is parsed and mapped to LLVM `ptr`, but string values are rejected by semantic analysis
until string support is implemented.

### Declarations and constants

Both declaration forms accepted by the frontend are supported:

```drift
var i32 score = 100;
i32 total = score + 1;
const i32 maxPlayers = 32;
```

`const` variables cannot be reassigned. Parameters are mutable. Redeclaring a name in the same
scope follows the interpreter's "most recent declaration wins" behavior.

### Expressions

- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Unary: `-`, `!`
- Comparisons: `==`, `!=`, `<`, `<=`, `>`, `>=`
- Parentheses
- Function calls as expressions
- Literals: integers, floating point, `true`/`false`, character literals

Integer and floating-point literals adapt to an expected type. Constant numeric expressions can
also adapt, so `i32 value = 1 + 2 * 3;` is valid. Operands that are both variables must already
have the same Drift type; there are no general implicit conversions.

### Statements

- Variable and constant declarations
- Assignment
- `return`
- `if` / `elif` / `else`
- `while`
- Function calls as statements

### Functions

Functions may have parameters and return values, call functions declared later in the file, and
recurse. Duplicate function names, including names that differ only by case, are rejected. Nested
function declarations are not supported.
The compiler uses the interpreter's existing parameter syntax but requires type information for
every parameter.

### Diagnostics

Frontend, semantic, and code-generation errors are reported with the source path and, when
available, line and column:

```text
examples/broken.drf:3:12: error: Undefined variable 'missing'.
```

Toolchain failures include the failed clang/linker command and its output.

## Unsupported features

The following are intentionally rejected with a diagnostic rather than compiled incorrectly:

- `println`, `print`, `read`, and `readln`
- String values and string literals
- Arrays and array access
- `switch`
- `for`
- Global variables and any top-level statement other than a function declaration
- Nested functions
- Logical `&&` and `||`
- Explicit casts and non-literal implicit conversions
- Function overloading
- Parameterized entry points
- Floating-point or string entry-point return values
- Multi-file compilation, imports, external declarations, and a standard library
- Debug information and target/cross-compilation options

Some frontend constructs may not be parsed at all. The compiler only implements constructs that
the shared Drift frontend already accepts and that its semantic pass explicitly allows.

## Known semantic differences from the interpreter

Drift-LLVM emits ordinary LLVM operations; it does not add runtime checks that the interpreter
performs. On supported programs that stay within range, results match the interpreter. Outside
those conditions, native behavior follows LLVM and the host target:

- Signed integer overflow wraps in native code; the interpreter uses checked conversions and can
  raise a runtime error.
- Integer division or remainder by zero is undefined behavior in native code; the interpreter
  reports an error.
- Floating-point division by zero produces IEEE infinity/NaN in native code; the interpreter
  reports a runtime error.
- `f16` arithmetic depends on the target's floating-point support and may be emulated.
- `NaN` comparisons follow IEEE semantics; `!=` uses LLVM's unordered-not-equal predicate so that
  `NaN != NaN` is true, matching the interpreter's comparison behavior.
- The interpreter supports a superset of the language; unsupported features are compile-time
  errors here.

## Repository layout

```text
Drift-LLVM/
├── Drift.LLVM.csproj
├── Drift.LLVM.sln
├── src/
│   ├── Backend/        LLVM backend boundary and clang implementation
│   ├── Cli/            Command-line parsing
│   ├── CodeGen/        LLVM IR module and function emission
│   ├── Compilation/    Compiler pipeline and options
│   ├── Diagnostics/    Source-located compiler exceptions
│   ├── Linking/        Native linking
│   ├── Semantics/      Drift types, symbols, scopes, semantic model
│   ├── Toolchain/      clang discovery and process execution
│   └── Program.cs      CLI entry point
├── examples/           Verified Drift programs
└── tests/
    └── Drift.LLVM.Tests/
```

The compiler does not reference the interpreter's runtime or evaluation classes. It only uses
the shared language frontend (lexer, parser, and AST).

## Tests

Run the compiler tests:

```bash
dotnet test tests/Drift.LLVM.Tests/Drift.LLVM.Tests.csproj
```

Run the interpreter's tests to confirm the shared frontend still works:

```bash
dotnet test ../Drift-Interpreter/tests/DriftInterpreter.Tests/DriftInterpreter.Tests.csproj
```

The native tests require clang. When clang is not available they return without executing native
steps, so a full release check should require clang explicitly. The test suite covers:

- frontend reuse and parameter-type preservation
- Drift-to-LLVM type mapping
- semantic diagnostics and literal adaptation
- IR structure
- IR verification and rejection of malformed modules
- object and IR artifact emission
- CLI behavior and diagnostics
- native executable generation and exit codes
- running a shared supported program through both the interpreter and the native compiler

## Design constraints

- Reuse the interpreter frontend; do not duplicate lexer, parser, AST, or type syntax.
- Keep the interpreter functional; the only shared-source change is the parameter metadata needed
  for native compilation.
- Keep LLVM details out of language-level types and semantics.
- Keep the backend behind an interface so the textual clang path can be replaced by a direct LLVM
  API later.
- Prefer clear diagnostics over silently compiling unsupported or unsafe semantics.
- Do not claim native support for a feature unless an executable has been generated and tested.

## Further documentation

- [architecture.md](architecture.md) — compiler pipeline and component responsibilities
- [development.md](development.md) — building, testing, and extending the compiler
- [llvm.md](llvm.md) — LLVM IR generation, toolchain commands, and backend design
