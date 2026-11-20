# C++ LLVM AOT compiler

The C++ component parses the Hybrid C-like grammar in
[`cpp/grammars/hybrid.y`](../cpp/grammars/hybrid.y), emits verified LLVM IR, and
can write a native object file for the host target. Despite the historical
`Jit` class name, this API does not execute generated code or implement an
in-process JIT. Object files are intended for a normal external linker.

## Requirements and build

Requirements: CMake 3.16+, a C++17 compiler, Bison 3.8+, and LLVM 18+ development
libraries with the host target enabled.

```sh
cmake -S cpp -B build/cpp
cmake --build build/cpp
ctest --test-dir build/cpp --output-on-failure
```

The library target is `hybrid_jit`; include `hybridlang/jit.hpp` and link the
target. The `example_emit_object` program writes `hybrid-program.o`.
`example_native_functions` and `example_host_class` print IR for inspection.

## Compile and emit

```cpp
#include "hybridlang/jit.hpp"

int main() {
    hybridlang::Jit compiler;
    compiler.compile("let answer = 6 * 7;");
    const auto& ir = compiler.emitIR();
    compiler.writeObject("program.o");
}
```

`compile` parses and type-checks a source string and stores its verified LLVM
IR. `emitIR()` returns that IR; `writeObject(path)` asks LLVM to emit a host
object file. Build and link the resulting object using your normal toolchain.
The compiler does not launch a linker, execute the object, or load generated
machine code into the current process.

## Register external functions

Host functions are represented as explicit source-name-to-linker-symbol
bindings with a C ABI signature. Supported ABI types are `Void`, `I64`, `F64`,
`Bool`, and opaque `Pointer`.

```cpp
using hybridlang::Signature;
using hybridlang::Type;

compiler.registerFunction(
    "std::square",
    "host_square",
    Signature{Type::I64, {Type::I64}}
);
compiler.compile("let answer = std::square(9);");
compiler.writeObject("program.o");
```

This declares `host_square` in the generated IR; the application must provide
that symbol when linking. Registration does not search for or execute an
arbitrary C++ symbol. Numeric `I64`/`F64` arguments can be converted at the
call site; other argument types must match.

## Register external classes

Classes use an opaque pointer ABI. A constructor returns a pointer. A method
symbol receives the object pointer as its first argument. Fields use explicit
getter and setter symbols, so no C++ layout or private member access is
assumed.

```cpp
compiler.registerClass(
    "Counter", "counter_create",
    Signature{Type::Pointer, {Type::I64}}
);
compiler.registerMethod(
    "Counter", "increment", "counter_increment",
    Signature{Type::I64, {Type::Pointer, Type::I64}}
);
compiler.registerField(
    "Counter", "value", "counter_get_value", "counter_set_value", Type::I64
);
compiler.compile(
    "let count = Counter(10); "
    "count.increment(2); "
    "count.value = count.value + 1;"
);
```

The example declares external functions; it does not embed C++ objects or
callbacks in the object file. The application supplies the matching ABI
implementations at link time. Hybrid source does not declare classes,
inheritance, virtual methods, or object layouts.

## C++ grammar subset

The Bison grammar accepts:

- Scalar `let`, `var`, `const`, and typed declarations with an initializer;
  supported types are `int`, `long`, `float`, `double`, `bool`, and `auto`.
- Assignments, numeric and boolean literals, variable references, and
  arithmetic/comparison/logical/bitwise operators.
- Brace-delimited `if`/`else` and `while`.
- Calls to explicitly registered functions/classes, class method calls, and
  registered scalar field reads/writes.
- `std::name(...)` as a qualified source alias when that exact alias is
  registered.

The source is a single top-level program; it does not support string values,
source-defined functions, source-defined classes, arrays, templates,
exceptions, arbitrary C++ syntax, or the full standards of C, C++, Python, Go,
BASIC, COBOL, or JavaScript. Separate Python teaching frontends accept the
limited per-language forms listed in [the language manual](manual.md); they do
not feed this LLVM backend.

## Safety and output boundaries

The generated object is ordinary target-specific machine code. Treat it as
untrusted executable input if you choose to link and run it. This library only
produces LLVM IR and object files: it does not allocate executable memory,
change page permissions, or invoke generated code.
