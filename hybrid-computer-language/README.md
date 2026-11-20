# Hybrid Computer Language

A compiler prototype inspired by the **Hybrid Computer Language Parser** and
**JIT Engine Interface** PDFs. It demonstrates how several source syntaxes can
lower to a validated execution model, with a Python frontend and a C++17
bytecode runtime.

> **Scope:** this is an educational subset compiler, not a C/C++/Python/Go/BASIC/
> COBOL/JavaScript implementation. Each frontend supports only the constructs in
> [the language manual](docs/manual.md). It does not emit native machine code.

The Python compiler parses restricted source subsets and compiles validated
expressions to CPython bytecode. The C++ SDK compiles its Hybrid C-like syntax
to a portable, verified `.hbc` binary and executes it in a bounded VM. Neither
backend emits native machine code. C++ host functions and classes are available
only when explicitly registered by the embedding application.

## Install

The Python CLI requires Python 3.10 or later and has no runtime dependencies.
The C++ SDK requires a C++17 compiler and CMake 3.16 or later.

```sh
python -m pip install .
```

## Run a program

```sh
hybridlang examples/c/sum.c
hybridlang --language python examples/python/sum.py
hybridlang --check examples/javascript/sum.js
```

Run without installing:

```sh
PYTHONPATH=src python -m hybridlang examples/python/sum.py
```

## C++ SDK

```sh
cmake -S cpp -B build/cpp
cmake --build build/cpp
ctest --test-dir build/cpp --output-on-failure
(cd build/cpp && ./example_native_functions && ./example_host_class && ./example_binary_roundtrip)
```

See [the C++ SDK guide](docs/cpp-sdk.md) for the header/source API, portable
binary format, callback registration, and host-class bindings. The Python
frontends accept the seven documented teaching subsets; the C++ SDK accepts
the separate Hybrid C-like subset described there.

`--language auto` (the default) uses the file extension first and then a few
source markers. Pass `--language` explicitly when detection is ambiguous. Use
`--max-steps N` to tune the execution budget; loops stop with an error when the
limit is exceeded.

## Project contents

- `src/hybridlang/` — language frontends, AST validation, expression JIT, and CLI.
- `cpp/include/hybridlang/jit.hpp` and `cpp/src/jit.cpp` — C++ API and bytecode VM.
- `cpp/examples/` — native callback, host-class, and binary round-trip examples.
- `cpp/tests/` — C++ runtime and serialization tests.
- `examples/` — runnable examples for the seven syntax subsets.
- `tests/` — focused compiler, runtime, diagnostic, and budget tests.
- `docs/manual.md` — supported syntax and implementation boundaries.
- `docs/usage.md` — CLI and Python API usage.
- `docs/cpp-sdk.md` — C++ embedding and binary usage.
- `docs/pdf-analysis.md` — analysis of the source PDFs and scope decisions.

## Validate

```sh
PYTHONPATH=src python -m unittest discover -s tests -v
```

## License

MIT. See [LICENSE](LICENSE).
