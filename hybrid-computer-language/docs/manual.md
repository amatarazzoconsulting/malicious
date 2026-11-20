# Language and compiler manual

## What this project implements

The source PDFs describe a multi-language compiler, a unified AST, and a JIT,
but do not define a complete grammar or provide parser/compiler source code.
This project therefore implements deliberately narrow teaching subsets for
seven source styles in its Python frontend: C, C++, Python, Go, BASIC, COBOL,
and JavaScript. A separate C++17 SDK implements the Hybrid C-like syntax and
portable bytecode runtime; it does not parse all seven source syntaxes.

The Python frontends lower to a shared statement model with variables, output,
conditionals, bounded `while` loops, arithmetic, comparisons, boolean logic, and
bitwise operations. That frontend does **not** implement classes, inheritance,
pointers, arrays, exceptions, or general-purpose host API access. Type
declarations are accepted as syntax but types are dynamic at runtime.

The C++ SDK additionally supports calls to explicitly registered host functions
and instances of explicitly registered host C++ classes, including selected
fields and methods. Hybrid source does not declare classes itself; inheritance,
virtual dispatch, and arbitrary host symbol lookup are not implemented.

## Python runtime

The compiler validates source expressions and compiles those expressions into
CPython bytecode functions at compile time. Program statements are executed by
the Hybrid runtime; the default budget is 100,000 executed statements and
expressions. This is a bytecode expression JIT, not native machine-code
generation, and its performance is not claimed to match an optimizing native
compiler.

The Python runtime exposes only variable storage and output.

## C++ bytecode runtime

The C++ SDK is implemented in [jit.hpp](../cpp/include/hybridlang/jit.hpp) and
[jit.cpp](../cpp/src/jit.cpp). It compiles the documented Hybrid C-like syntax
into verified stack bytecode and can write/read portable `.hbc` binary files.
The `HYBC` file format is versioned, little-endian, and contains bytecode only;
host callbacks and C++ objects must be registered again when loading it.
Execution uses a VM with a configurable instruction budget. It does **not**
generate native CPU machine code.

`registerFunction` and `registerKeyword` bind a source-visible function name to
a C++ callback. Only callbacks explicitly registered by the host application
can run. `addNativeClass<T>`, `addNativeMethod<T>`, and `addNativeField<T>`
expose selected operations on a host C++ type. The runtime does not look up
arbitrary C++ symbols or grant callbacks additional permissions automatically.

## Supported constructs

### Expressions

- Integer, floating-point, string, and boolean literals.
- Variable reads; variables are created by assignment or declaration.
- `+`, `-`, `*`, `/`, `//`, `%`.
- `&`, `|`, `^`, `~`, `<<`, `>>`.
- Unary `+`, `-`, `!` / `not`.
- `==`, `!=`, `<`, `<=`, `>`, `>=`.
- `&&`, `||` / `and`, `or`.

Division follows Python semantics: `/` produces floating-point results when
appropriate; Python's `//` is floor division. Other frontends accept `/`.
Bitwise operators require integer values. Integer results are limited to 4096
bits, string values to 100,000 characters, and captured output to 1,000,000
characters.

### Statements

- Variable assignment and compound `+=`, `-=`, `*=`, `/=`, `%=`.
- `print(...)`, `println(...)`, Python `print(...)`, Go `fmt.Println(...)`,
  JavaScript `console.log(...)`, C `printf(...)`, C++ `std::cout << ...`.
- `if` / `else` conditionals (Python indentation or brace blocks).
- `while` loops; Go `for condition { ... }` and COBOL `PERFORM UNTIL` lower to
  the same loop statement.

## Frontend-specific subset

| Python frontend | Accepted forms |
| --- | --- |
| C | `int main() { ... }`, scalar declarations, assignments, `if`/`else`, `while`, and a limited `printf` format subset |
| C++ | C-like statements and `std::cout << expression << ... << std::endl;` |
| Python | assignments, augmented assignments, `print`, `if`/`else`, and `while`; no `for`, definitions, or imports |
| Go | optional `package`/`import` header, `func main()`, `var`, `:=`, `fmt.Println`, `if`, and condition-only `for` |
| BASIC | `LET`, assignment, `PRINT`, `IF ... THEN` / `ELSE` / `END IF`, `WHILE` / `WEND` |
| COBOL | selected division headers, `MOVE ... TO`, `COMPUTE`, `DISPLAY`, `IF` / `ELSE` / `END-IF`, and `PERFORM UNTIL` / `END-PERFORM` |
| JavaScript | `let` / `const` / `var`, assignments, `console.log`, brace-style `if` / `else`, and `while` |

These are teaching dialects, not drop-in parsers for the corresponding
standards. See [examples/](../examples/) for the accepted syntax.

## C++ SDK source subset

The C++ SDK accepts statements in a separate C-like syntax: `let`/`var` and
scalar declarations, assignment, function calls, member field/method calls,
brace-delimited `if`/`else` and `while`, and the operators listed above. Each
statement ends in `;`. It supports no functions or class declarations in Hybrid
source; host functions/classes are registered from C++ instead. See
[the C++ SDK guide](cpp-sdk.md) and [C++ examples](../cpp/examples/).

## C `printf`

The first argument must be a string literal. Supported conversions are `%d`,
`%i`, `%u`, `%x`, `%X`, `%f`, `%s`, `%c`, and `%%`, with optional numeric width
and floating precision. The subset does not support pointers, length modifiers,
or variadic host calls.

## Diagnostics and limits

Syntax and unsupported constructs report a source line where available. Runtime
errors include the relevant statement line. The CLI's `--max-steps` option
limits executed statements and expression evaluations; a non-positive value is
rejected.
