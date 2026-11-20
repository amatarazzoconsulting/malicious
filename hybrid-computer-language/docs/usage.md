# Usage guide

## CLI

Install the project with `python -m pip install .`. Then run a source file:

```sh
hybridlang examples/go/sum.go
hybridlang --language cobol examples/cobol/sum.cob
hybridlang --check examples/cpp/sum.cpp
hybridlang --max-steps 500 examples/basic/sum.bas
```

The `--language` option accepts `auto`, `c`, `cpp`, `python`, `go`, `basic`,
`cobol`, and `javascript`. Auto-detection checks known file extensions before
using content hints. Specify the language for extensionless or ambiguous input.

`--check` compiles the program without executing it. By default, programs have
a 100,000-step budget. A loop that exceeds that budget fails with a diagnostic.

## Python API

```python
from hybridlang import compile_source

program = compile_source(
    "let answer = 6 * 7; println(answer);",
    language="javascript",
)
print(program.run(max_steps=1000), end="")
```

`compile_source(source, language="auto", filename=None)` returns a
`CompiledProgram`. `CompiledProgram.run(max_steps=100_000)` returns captured
standard output. Compilation failures raise `CompileError`; execution failures
raise `ExecutionError` (or its `RuntimeLimitError` subclass).

## C++ SDK

The C++ library is built with CMake and requires C++17:

```sh
cmake -S cpp -B build/cpp
cmake --build build/cpp
ctest --test-dir build/cpp --output-on-failure
```

Run all three embedding samples from the build directory:

```sh
cd build/cpp
./example_native_functions
./example_host_class
./example_binary_roundtrip
```

The last sample writes `hybrid-demo.hbc` to its current working directory. For
API examples and supported syntax, see [the C++ SDK guide](cpp-sdk.md).

## Examples

Each syntax subdirectory in `examples/` contains a `sum` program. Run them with
the CLI commands from the project README. Programs are intentionally small so
the supported syntax and common IR are easy to inspect.
