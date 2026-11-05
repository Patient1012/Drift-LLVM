# Architecture

Drift-LLVM is split into a reusable language frontend, a compiler-specific semantic layer, a
language-to-LLVM lowering layer, and a host toolchain boundary. The compiler deliberately does not
depend on the interpreter's runtime.

## Pipeline

```text
.drf source
    │
    ▼
Lexer                  shared Drift-Interpreter frontend
    │
    ▼
Parser                 shared Drift-Interpreter frontend
    │
    ▼
AST                    shared Drift-Interpreter AST
    │
    ▼
DriftSemanticAnalyser  compiler-specific validation and symbol resolution
    │
    ▼
SemanticModel          expression types, variable/call/declaration bindings
    │
    ▼
ModuleEmitter          textual LLVM IR
    │
    ▼
ClangLLVMBackend       LLVM verification + object generation
    │
    ▼
NativeLinker           native executable via clang's driver
    │
    ▼
Executable
```

The orchestration is in `src/Compilation/DriftCompiler.cs`:

- `CompileToIr` runs lexing, parsing, semantic analysis, and IR emission.
- `Build` additionally resolves clang, verifies the module, emits an object file, links an
  executable, and optionally preserves requested IR/object artifacts.

`CompilationUnit` exposes the parsed program, the semantic model, and the generated IR for tests
and future tooling.

## Frontend reuse

The compiler references `../Drift-Interpreter/src/DriftInterpreter.csproj` directly. It uses the
interpreter's existing `Lexer`, `Parser`, and AST types. There is no duplicate lexer, parser, type
syntax, or AST.

The only shared-source change made for native compilation is parameter metadata:

- `Drift-Interpreter/src/AST/Statements/Parameter.cs` adds a small `Parameter` node containing a
  type, name, and source location.
- `FunctionDeclaration` keeps its existing `Parameters` list and adds
  `ParameterDeclarations`.
- `Parser.ParseFunction` populates both lists while parsing.

The interpreter continues to use `Parameters` and its behavior is unchanged. The native compiler
uses `ParameterDeclarations` because it needs each parameter's declared type. The interpreter's
test suite is run after the change to verify this.

Shared frontend types are in the global namespace for historical reasons. Compiler types live in
`Drift.LLVM.*` namespaces. In particular, the compiler semantic analyser is named
`DriftSemanticAnalyser` to avoid colliding with the interpreter's global `SemanticAnalyser`
placeholder.

## Language-level type model

`src/Semantics/DriftType.cs` models the language types independently of LLVM:

- integer widths and signedness
- floating widths
- `bool`, `char`, `str`, and `void`

This is compiler-specific rather than a reuse of the interpreter's runtime `ValueType`: the
interpreter's enum is a runtime value tag that includes arrays, functions, and null, and the
compiler is not allowed to depend on interpreter runtime classes. The source spelling and
signedness rules still come from the shared language and specification.

The only LLVM-facing member is `LlvmName`, which maps a Drift type to the corresponding scalar LLVM
type. Code generation and semantic rules use the same type objects, so aliases and spelling
differences cannot silently diverge.

`DriftType.Parse` rejects unknown type names with a `SemanticException` at the declaration
location. Semantic analysis rejects value types that cannot be represented yet (`str`, `void`) and
entry-point return types that cannot be converted to the C `main` result.

## Semantic analysis

`src/Semantics/DriftSemanticAnalyser.cs` validates the parsed program and produces a read-only
`SemanticModel`. It performs work that the interpreter does dynamically:

- rejects top-level statements
- collects function declarations and checks for duplicate names, including names that differ only by
  case
- locates the case-insensitive `main` entry point
- checks entry-point parameters and return type
- resolves parameter and local types
- resolves lexical scopes
- resolves every variable reference, assignment target, call, and declaration
- checks constness
- checks all expression and statement types
- adapts integer/float literals to expected types
- checks argument counts and parameter types
- checks that non-void functions return on all supported paths
- rejects unsupported statements and expressions with source-located diagnostics

The semantic model is the contract between analysis and code generation. Code generation does not
repeat scope lookup or type inference. It calls:

- `TypeOf(Expression)`
- `VariableOf(VariableExpression)`
- `AssignmentTarget(AssignmentStatement)`
- `CallTarget(CallExpression)`
- `DeclarationSymbol(VarDeclaration)`
- `FunctionOf(FunctionDeclaration)`

`Scope` uses ordinal name comparison and follows the interpreter's same-scope rule: the most
recent declaration of a name wins. Child scopes are used for `if`, `elif`, `else`, and `while`
bodies.

### Numeric typing rules

The compiler is stricter than the interpreter:

- `bool` is required for `if`, `elif`, and `while` conditions.
- `<`, `<=`, `>`, and `>=` require numeric operands.
- `==` and `!=` require equal types (or compatible numeric literals).
- Arithmetic requires equal operand types after literal adaptation.
- There are no general implicit conversions.
- Constant numeric expressions and literals may be re-analysed as the expected type in a mixed
  literal/variable expression.

This keeps native lowering predictable and avoids inventing conversion semantics that the Drift
specification does not define.

### Return checking

`AlwaysReturns` is intentionally conservative. A non-void function is accepted when its statement
list contains a `return`, or an `if` with an `else` where every branch returns. It does not try to
prove that arbitrary `while` loops terminate or that later statements are unreachable. This rejects
some programs that are semantically total, but it never accepts a non-void function with a
reachable fallthrough.

## Code generation

`src/CodeGen/ModuleEmitter.cs` assembles the module:

1. Module metadata and an `llvm.trap` declaration.
2. One `define` per Drift function.
3. A C `@main` wrapper for the Drift entry point.

`src/CodeGen/FunctionEmitter.cs` emits one function:

- Every parameter and local gets an LLVM `alloca` in the entry block.
- Parameters are stored from their ABI values into their allocas.
- Variables are loaded and stored on demand.
- Expressions are emitted in source order.
- `if`/`elif`/`else` becomes labeled branches.
- `while` becomes a condition block, body block, and end block.
- Calls target the mangled Drift function name.
- A non-void function that somehow reaches the end gets a trap and `unreachable`; semantic analysis
  should prevent this from being reachable.
- A void function ends with `ret void`.

Drift function names are ASCII-mangled with a `drift_` prefix. ASCII letters and digits are kept;
underscores and other characters are escaped as `_uXXXX`. Escaping underscores prevents a source
name such as `_u03B1` from colliding with the escaped form of U+03B1, so distinct Drift function
names always produce distinct LLVM symbols. The wrapper is emitted as the C-ABI `main` and calls the
mangled Drift entry point.

### Entry-point conversion

The wrapper must turn the Drift return value into the C `int` result:

| Drift return type | Wrapper conversion |
| --- | --- |
| `void` | call, then `ret i32 0` |
| `bool` | `zext i1` to `i32` |
| `char` | already represented as `i32` |
| integer width < 32 | `sext` if signed, `zext` if unsigned |
| integer width = 32 | use directly |
| integer width > 32 | `trunc` to `i32` |
| float/string | rejected during semantic analysis |

The process exit code is the platform's interpretation of that `i32`; on POSIX systems it is
truncated to the low 8 bits by the operating system.

## LLVM backend boundary

`src/Backend/ILLVMBackend.cs` separates "turn LLVM IR into an object" from the rest of the compiler.
The current implementation, `ClangLLVMBackend`, shells out to clang:

- parse textual IR
- run LLVM's verifier
- emit an object file
- apply the selected optimization level

This boundary is the place to add a direct LLVM API implementation later (LLVMSharp, the LLVM C
API, or another maintained binding) without changing the frontend, semantic model, or IR builders.
The current textual implementation is intentional: it is portable, inspectable, and avoids tying
the proof of concept to a particular native LLVM library version.

`src/Linking/NativeLinker.cs` invokes clang's driver on the object file. The driver supplies the
platform C runtime and system linker. On Unix-like systems the linker also receives `-lm` because
LLVM lowers floating-point remainder (`%` on floats) to `fmod`.

See [llvm.md](llvm.md) for the exact IR commands and instruction mapping.

## Diagnostics

Compiler diagnostics are source-located:

- Frontend parser/lexer exceptions already carry `SourcePosition`.
- `SemanticException` carries the statement or expression location.
- `CodeGenException` carries the location of the construct that could not be lowered.
- `ToolchainException` reports the external command and its output.

The CLI formats frontend and semantic errors as:

```text
<file>:<line>:<column>: error: <message>
```

Toolchain failures are intentionally not given a fake source location. They include clang's or the
linker's diagnostics so the actual LLVM/IR/toolchain cause is visible.

## Linker and platform assumptions

- The compiler targets the host platform; it does not emit a target triple or accept a target
  option. clang chooses the host target.
- Textual LLVM IR uses opaque pointers (`ptr`), so LLVM 15 or newer is required.
- clang's driver is also used as the linker. This is a pragmatic way to get the C runtime without
  hardcoding platform paths or linker names.
- The C `main` wrapper is the only symbol guaranteed to follow the host C ABI. Drift-to-Drift calls
  use LLVM's internal calling convention and are not a stable external ABI.

## Boundaries and future work

The current architecture makes the following extensions straightforward:

- Add a language construct in the semantic analyser, semantic model, function emitter, and tests.
- Add an `ILLVMBackend` implementation that builds modules through a direct LLVM API.
- Add a release build mode or multi-file compilation on top of `DriftCompiler`.
- Add target selection by extending `CompilerOptions` and the backend, without leaking target
  details into `DriftType` or semantic analysis.
- Add debug information by extending `ModuleEmitter` and the backend.

The architecture deliberately avoids:

- interpreter runtime dependencies in the compiler
- custom assembly generation
- platform-specific path assumptions
- LLVM type names in the semantic rules beyond the single `LlvmName` mapping
- optimizing before the language semantics and diagnostics are correct
