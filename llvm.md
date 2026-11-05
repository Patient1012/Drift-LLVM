# LLVM Backend

Drift-LLVM emits textual LLVM IR and delegates verification, optimization, object generation, and
linking to the host clang toolchain. This is the current proof-of-concept backend. The compiler
pipeline is structured so the textual layer can be replaced by a direct LLVM API without changing
the frontend or semantic model.

## Why textual IR first

The initial goals are correctness, inspectability, and a working native executable, not a
library-specific integration:

- Textual IR is easy to inspect and commit to test expectations.
- clang performs LLVM verification and target lowering.
- The compiler does not need to ship or pin a native LLVM library.
- The generated IR can be diffed and debugged independently of the compiler.
- The backend interface keeps the rest of the compiler independent of this decision.

The trade-off is an external `clang` process per build, no direct control over LLVM data
structures, and a requirement for clang on the build machine. Those are acceptable for the
current proof of concept.

## Pipeline and commands

For a source file `program.drf`:

```text
program.drf
  └── DriftSemanticAnalyser
        └── ModuleEmitter
              └── module.ll
                    ├── clang -c -x ir module.ll -o module.verify.o -O<level> -Wno-override-module
                    ├── clang -c -x ir module.ll -o module.o        -O<level> -Wno-override-module
                    └── clang module.o -o program -O<level> [-lm on Unix-like systems]
```

Verification and object generation both parse the IR through clang. The verification step emits a
temporary object and deletes it; success means LLVM accepted and lowered the module. It is not a
separate abstract verifier invocation.

On Unix-like systems the linker is given `-lm` because LLVM lowers `frem` (floating-point `%`) to
a call to `fmod`, which is provided by the math library. The flag is harmless when unused. On
Windows, the C runtime is supplied by clang's driver and `-lm` is not added.

The compiler does not emit a target triple. clang selects the host target and target-specific data
layout. `-Wno-override-module` suppresses a warning about the module lacking a target triple; it
does not suppress LLVM errors.

## Opaque pointers

The emitter uses LLVM's `ptr` type for local variable addresses, so the generated module requires
LLVM 15 or newer. There is no typed-pointer alternative in the current emitter. Supporting older
LLVM versions would require a deliberate compatibility mode, not just a version check.

## Type mapping

| Drift type | LLVM type | Notes |
| --- | --- | --- |
| `i8` | `i8` | signed operations |
| `i16` | `i16` | signed operations |
| `i32` | `i32` | signed operations |
| `i64` | `i64` | signed operations |
| `ui8` | `i8` | unsigned comparisons/division/remainder |
| `ui16` | `i16` | unsigned comparisons/division/remainder |
| `ui32` | `i32` | unsigned comparisons/division/remainder |
| `ui64` | `i64` | unsigned comparisons/division/remainder |
| `f16` | `half` | target may emulate |
| `f32` | `float` | |
| `f64` | `double` | |
| `uf16` | `half` | unsigned float, same IEEE representation |
| `uf32` | `float` | unsigned float, same IEEE representation |
| `uf64` | `double` | unsigned float, same IEEE representation |
| `bool` | `i1` | `true`/`false` |
| `char` | `i32` | single UTF-16 code unit stored as an integer |
| `str` | `ptr` | mapped but rejected by semantic analysis |
| `void` | `void` | return type only |

LLVM integer types are signless. Signedness is encoded in the operation selected by the emitter:
`icmp slt` versus `icmp ult`, `sdiv` versus `udiv`, `srem` versus `urem`, and so on.

## Module structure

A typical module starts with:

```llvm
; ModuleID = 'drift'
source_filename = "return42.drf"

declare void @llvm.trap()
```

Drift functions are emitted first, followed by a C-ABI wrapper:

```llvm
define i32 @drift_main() {
entry:
  ret i32 42
}

define i32 @main() {
entry:
  %entry_result = call i32 @drift_main()
  ret i32 %entry_result
}
```

### Name mangling

Drift identifiers may contain Unicode letters. `NameMangler` converts every function name to an
ASCII-only symbol with a `drift_` prefix:

- ASCII letters and digits are kept.
- Underscores and other characters become `_uXXXX`, using the character's UTF-16 code unit in
  uppercase hex. Escaping underscores prevents a source name such as `_u03B1` from colliding with
  the escaped form of U+03B1.

This keeps symbols valid and reduces the chance of collisions with runtime symbols. The C wrapper is
always named `main`; Drift's `main` is mangled like every other function.

## Function lowering

Every Drift function has:

- an `entry` block
- one `alloca` for every parameter and local in that entry block
- parameter stores from ABI values into those allocas
- loads/stores for variable reads and assignments
- labeled branches for control flow

Parameters are passed as scalar LLVM values. No structs, arrays, or C ABI layout rules are used for
Drift-to-Drift calls. This is an internal calling convention; only the generated `main` wrapper is a
stable C entry point.

### Statements

| Drift statement | Lowering |
| --- | --- |
| variable declaration | evaluate initializer, `store` into the local `alloca` |
| assignment | evaluate value, `store` into the bound local |
| `return` | `ret` the function's LLVM return type |
| `if` / `elif` / `else` | condition block, branch per condition, branch to end |
| `while` | branch to condition, conditional branch to body/end, branch back |
| call statement | emit call, discard a non-void result |
| void fallthrough | `ret void` |

Nested functions and unsupported statements are rejected before lowering. A non-void function that
somehow reaches the end emits `call void @llvm.trap()` followed by `unreachable` rather than
returning a fake value. Semantic analysis should make that path unreachable.

### Expressions

| Drift expression | LLVM lowering |
| --- | --- |
| integer literal | integer constant, adapted to the expected Drift type |
| float literal | floating constant |
| boolean literal | `true` / `false` |
| character literal | numeric code point |
| variable read | `load` from its `alloca` |
| unary `-` | `fneg` for floats, `sub <type> 0, x` for integers |
| unary `!` | `xor i1 x, true` |
| `+`, `-`, `*` | `add`/`sub`/`mul` or `fadd`/`fsub`/`fmul` |
| `/` | `sdiv`/`udiv` or `fdiv` |
| `%` | `srem`/`urem` or `frem` |
| `==` | `icmp eq` or `fcmp oeq` |
| `!=` | `icmp ne` or `fcmp une` |
| `<`, `<=`, `>`, `>=` | signed/unsigned `icmp` or ordered `fcmp` |
| function call | `call` with scalar typed arguments |

`fcmp une` is used for `!=` so that `NaN != NaN` is true, matching IEEE and the interpreter's
comparison behavior. `==` uses ordered equality, so `NaN == NaN` is false.

## Entry point conversion

The generated `@main` must return C `int` (`i32`). The wrapper converts a non-void Drift result:

| Drift result | Conversion |
| --- | --- |
| `void` | call, then `ret i32 0` |
| `bool` | `zext i1` to `i32` |
| `char` | already `i32` |
| `i8`/`i16` | `sext` to `i32` |
| `ui8`/`ui16` | `zext` to `i32` |
| `i32`/`ui32` | use the value directly |
| `i64`/`ui64` | `trunc` to `i32` |
| float/string | rejected before code generation |

The host process exit code is derived from the low bits of the `i32` result. On POSIX systems an
exit code is conventionally interpreted modulo 256; tests therefore assert values that fit the
convention rather than relying on wide exit codes.

## Optimization

The optimization level is passed to both object generation and linking with `-O<level>`, where the
level is 0 through 3. The default is `-O2`.

Optimization is intentionally decoupled from semantic correctness: the compiler must produce valid
IR at `-O0`, and the `-O2` path is covered by a native test. The proof of concept does not include
custom optimization passes or target-specific tuning.

## Diagnostics and verification

A malformed module, an LLVM type error, or an unsupported IR construct produces a
`ToolchainException` from `ClangLLVMBackend`:

```text
error: LLVM verify failed.
'/path/to/clang' failed with exit code 1:
...clang/LLVM diagnostic...
```

The compiler cannot always recover a source location from an LLVM diagnostic because textual IR
currently omits debug metadata. Improving this would require emitting `!dbg` information and using
LLVM's source-location diagnostics.

## Direct LLVM API path

The `ILLVMBackend` interface is the intended integration point:

```csharp
public interface ILLVMBackend
{
    string Name { get; }
    void Verify(string llvmIrPath, int optimizationLevel);
    void CompileToObject(string llvmIrPath, string objectPath, int optimizationLevel);
}
```

A future backend could:

1. Replace `ModuleEmitter`'s textual output with `LLVMContext`/`Module`/`IRBuilder` construction, or
   parse the textual module into an in-memory module.
2. Run the LLVM verifier directly.
3. Use a target machine and pass manager for optimization and object emission.
4. Link with the platform linker or an LLVM linker.

That work is not required for the current milestone. The investigation for this project found that
the available LLVMSharp packages are managed bindings that require a matching native LLVM library;
the development machine did not have `llvm-config` or an unversioned `libLLVM.so`, and older
bindings expected an unavailable LLVM 16. Textual IR plus clang is the pragmatic, reproducible
choice until a native LLVM dependency is explicitly provisioned.

## Undefined behavior and runtime differences

The emitter does not insert checks for:

- integer division or remainder by zero
- signed integer overflow
- invalid shifts (shifts are not currently in the expression grammar)

In native LLVM code these are undefined behavior or target traps, whereas the interpreter reports a
runtime error. Programs should stay within the documented type ranges until checked arithmetic is
introduced. Floating-point division by zero produces IEEE infinity/NaN natively, while the
interpreter reports an error.

## Not emitted yet

- target triple and target-specific data layout
- debug metadata (`!dbg`, `DILocation`, `DICompileUnit`)
- source-level line tables in LLVM diagnostics
- exceptions and unwinding
- a stable external ABI for Drift functions
- cross-compilation or target selection
- strings, arrays, and runtime library calls
