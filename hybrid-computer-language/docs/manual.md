# Language and compiler manual

## What this project implements

The source PDFs describe a multi-language compiler and JIT, but provide neither
compiler source nor complete formal grammars. This repository implements
limited teaching subsets for C, C++, Python, Go, BASIC, COBOL, and JavaScript in
the Python frontend. The C++ LLVM backend is a separate compiler for the
Hybrid C-like grammar in [`cpp/grammars/hybrid.y`](../cpp/grammars/hybrid.y);
it does not parse those seven source syntaxes.

The Python frontends lower to a shared statement model with variables, output,
conditionals, bounded `while` loops, arithmetic, comparisons, boolean logic,
and bitwise operations. They do not implement classes, inheritance, pointers,
arrays, exceptions, or general-purpose host API access. Type declarations are
accepted as syntax, but types are dynamic at runtime.

The C++ backend emits verified LLVM IR and host-target object files. External
functions and opaque-pointer classes can be registered with explicit
signatures. The application supplies the matching native symbols when linking
the object. The backend does not execute generated code in-process.

## Python runtime

The compiler validates source expressions and compiles those expressions into
CPython bytecode functions at compile time. Program statements are executed by
the Hybrid runtime; the default budget is 100,000 executed statements and
expressions. This is a bytecode expression JIT, not native machine-code
generation.

The Python runtime exposes only variable storage and output.

## Python frontend constructs

### Expressions

- Integer, floating-point, string, and boolean literals.
- Variable reads; variables are created by assignment or declaration.
- `+`, `-`, `*`, `/`, `//`, `%`.
- `&`, `|`, `^`, `~`, `<<`, `>>`.
- Unary `+`, `-`, `!` / `not`.
- `==`, `!=`, `<`, `<=`, `>`, `>=`.
- `&&`, `||` / `and`, `or`.

Division follows Python semantics: `/` produces floating-point results when
appropriate; Python's `//` is floor division. Bitwise operators require
integer values. Integer results are limited to 4096 bits, strings to 100,000
characters, and captured output to 1,000,000 characters.

### Statements

- Variable assignment and compound `+=`, `-=`, `*=`, `/=`, `%=`.
- `print(...)`, `println(...)`, Python `print(...)`, Go `fmt.Println(...)`,
  JavaScript `console.log(...)`, C `printf(...)`, and C++
  `std::cout << ... << std::endl;`.
- `if` / `else` conditionals (Python indentation or brace blocks).
- `while` loops; Go `for condition { ... }` and COBOL `PERFORM UNTIL` lower to
  the same loop statement.

## Python frontend-specific subsets

| Frontend | Accepted forms |
| --- | --- |
| C | `int main() { ... }`, scalar declarations, assignments, `if`/`else`, `while`, and a limited `printf` format subset |
| C++ | C-like statements and `std::cout << expression << ... << std::endl;` |
| Python | assignments, augmented assignments, `print`, `if`/`else`, and `while`; no `for`, definitions, or imports |
| Go | optional `package`/`import` header, `func main()`, `var`, `:=`, `fmt.Println`, `if`, and condition-only `for` |
| BASIC | `LET`, assignment, `PRINT`, `IF ... THEN` / `ELSE` / `END IF`, `WHILE` / `WEND` |
| COBOL | selected division headers, `MOVE ... TO`, `COMPUTE`, `DISPLAY`, `IF` / `ELSE` / `END-IF`, and `PERFORM UNTIL` / `END-PERFORM` |
| JavaScript | `let` / `const` / `var`, assignments, `console.log`, brace-style `if` / `else`, and `while` |

These are teaching dialects, not drop-in parsers for the corresponding
standards. See [`examples/`](../examples/) for the accepted syntax.

## C++ LLVM backend subset

The C++ backend accepts a separate, single-program Hybrid syntax: initialized
scalar declarations, assignments, literals, registered calls, class
construction and registered methods/fields, brace-delimited `if` / `else` and
`while`, and arithmetic, comparison, logical, and integer bitwise operators.
Statements end in `;`. Supported scalar LLVM ABI types are `i64`, `double`,
`i1`, and opaque pointers for class instances. Source-defined functions and
classes, strings, inheritance, arrays, templates, and arbitrary C++ syntax are
not supported. See [the C++ LLVM guide](cpp-sdk.md).

The `registerFunction`, `registerClass`, `registerMethod`, and `registerField`
API calls declare external link-time symbols; they do not execute callbacks or
discover host symbols. Class constructors and methods use an opaque-pointer
ABI, and fields map to explicit getter/setter functions.

## C `printf`

In the Python C frontend, the first argument must be a string literal.
Supported conversions are `%d`, `%i`, `%u`, `%x`, `%X`, `%f`, `%s`, `%c`, and
`%%`, with optional numeric width and floating precision. The subset does not
support pointers, length modifiers, or variadic host calls.

## Diagnostics and limits

Python syntax and unsupported constructs report a source line where available.
The CLI's `--max-steps` option limits executed statements and expression
evaluations; a non-positive value is rejected. The C++ parser limits source
input to 1 MB and string tokens to 100,000 characters, and reports line and
column diagnostics.
