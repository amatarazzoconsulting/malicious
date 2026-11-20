# Language and compiler manual

## What this project implements

The source PDFs describe a multi-language compiler, a unified AST, and a JIT,
but do not define a complete grammar or provide parser/compiler source code.
This project therefore implements a deliberately narrow common subset for seven
source styles: C, C++, Python, Go, BASIC, COBOL, and JavaScript.

Every frontend lowers to the same statement and expression model. The project
supports variables, output, conditionals, bounded `while` loops, arithmetic,
comparisons, boolean logic, and bitwise operations. It does **not** implement
functions (apart from an optional zero-argument C/C++/Go `main` wrapper),
classes, inheritance, pointers, arrays, imports, exceptions, or general-purpose
host API access. Type declarations are accepted as syntax but types are dynamic
at runtime.

## Runtime and JIT

The compiler validates source expressions and compiles those expressions into
CPython bytecode functions at compile time. Program statements are executed by
the Hybrid runtime; the default budget is 100,000 executed statements and
expressions. This is a bytecode expression JIT, not native machine-code
generation, and its performance is not claimed to match an optimizing native
compiler.

The runtime exposes only variable storage and output. It does not provide
filesystem, network, process, reflection, or arbitrary host-language calls.
The step budget bounds executed statements and expression evaluations, but it
is not an operating-system security boundary and does not guarantee bounded
memory or wall-clock use for every expression.

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
standards. See [examples/](../examples/) for the accepted syntax.

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
