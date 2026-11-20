# C++ SDK guide

## Build and test

Requirements: CMake 3.16+ and a C++17 compiler.

```sh
cmake -S cpp -B build/cpp
cmake --build build/cpp
ctest --test-dir build/cpp --output-on-failure
```

The library target is `hybrid_jit`; include
`hybridlang/jit.hpp` and link against that target.

## Compile and exchange values

```cpp
#include "hybridlang/jit.hpp"
#include <iostream>

int main() {
    hybridlang::Jit jit;
    jit.setInput(std::cin);
    jit.setOutput(std::cout);
    jit.setGlobal("seed", std::int64_t{21});
    jit.compile("let answer = seed * 2; println(answer);");
    jit.run();
    const auto answer = hybridlang::Jit::asInteger(jit.getGlobal("answer"));
}
```

`setGlobal` passes host values into a run; `getGlobal` retrieves values
afterward. The built-in `input()` reads one line from the configured input
stream. The `print` and `println` callbacks write to the configured output
stream. `run(maxSteps)` has a default 100,000-instruction budget.

## Register host functions

Source calls resolve only to built-ins or functions the host explicitly
registers. Qualified names such as `std::square` are source aliases, not
automatic C++ symbol lookup:

```cpp
jit.registerKeyword("std::square", [](const hybridlang::Arguments& args) {
    const auto value = hybridlang::Jit::asInteger(args.at(0));
    return hybridlang::Value{value * value};
});
jit.compile("let answer = std::square(9);");
jit.run();
```

`registerFunction` and `registerKeyword` both register an explicit callback;
the latter makes the source-visible alias intent clear. Host callbacks are
trusted application code and execute synchronously within the VM. The VM's
instruction budget cannot interrupt a callback that blocks or runs for a long
time.

## Bridge a C++ class

Classes are registered by the embedding application. Hybrid source can
construct a registered class, call its registered methods, and access
registered fields:

```cpp
struct Counter { std::int64_t value; };

jit.addNativeClass<Counter>("Counter", [](const hybridlang::Arguments& args) {
    return std::make_shared<Counter>(
        Counter{hybridlang::Jit::asInteger(args.at(0))}
    );
});
jit.addNativeMethod<Counter>(
    "Counter", "increment",
    [](Counter& self, const hybridlang::Arguments& args) -> hybridlang::Value {
        self.value += hybridlang::Jit::asInteger(args.at(0));
        return hybridlang::Value{self.value};
    }
);
jit.addNativeField<Counter>(
    "Counter", "value",
    [](const Counter& self) -> hybridlang::Value { return hybridlang::Value{self.value}; },
    [](Counter& self, const hybridlang::Value& value) {
        self.value = hybridlang::Jit::asInteger(value);
    }
);
jit.compile(
    "let count = Counter(10); count.increment(2); "
    "println(count.value);"
);
jit.run();
```

The source-language subset does not declare classes or inheritance. Registration
is explicit and limited to the constructor, methods, and fields chosen by the
host. See [`examples/host_class.cpp`](../cpp/examples/host_class.cpp).

## Portable bytecode binaries

`writeBinary("program.hbc")` writes a versioned `HYBC` bytecode file;
`loadBinary("program.hbc")` verifies and loads it. The binary contains code and
constants, not host callback implementations or live object instances. Register
the same required functions/classes in the runtime before running a loaded
program. The format is portable bytecode, not an operating-system executable
or native machine code.

`examples/binary_roundtrip.cpp` demonstrates writing and loading a file.

## C++ SDK language subset

This SDK compiles the Hybrid C-like syntax, not arbitrary C++ source. Supported
forms include:

- `let`, `var`, `const`, and scalar declarations, assignment, and literals.
- Arithmetic, comparisons, short-circuit `&&` / `||`, and integer bitwise
  operators.
- Brace-delimited `if` / `else` and `while` blocks.
- Calls to registered names (`std::name(...)` is allowed as a registered alias).
- Host object construction, registered field access, and registered method calls.
- `print(...)`, `println(...)`, and `input()`.

It does not support Hybrid class declarations, inheritance, templates, pointer
operations, arbitrary C++ syntax, or unregistered native calls. The separate
Python frontend accepts small teaching subsets of C, C++, Python, Go, BASIC,
COBOL, and JavaScript, as listed in [the language manual](manual.md).
Expression, block, and statement nesting is limited to 128 levels.
