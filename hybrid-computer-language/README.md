# Hybrid Computer Language

A compiler project inspired by the **Hybrid Computer Language Parser** and
**JIT Engine Interface** PDFs. It includes seven restricted Python teaching
frontends and a separate C++17/LLVM AOT compiler for a normalized Hybrid
C-like language.

> **Scope:** this is not a full implementation of C, C++, Python, Go, BASIC,
> COBOL, or JavaScript. The exact accepted forms are listed in
> [the language manual](docs/manual.md).

The Python CLI parses the seven teaching subsets, validates expressions, and
executes programs in its bounded runtime. The C++ component parses the
Hybrid-specific grammar, emits verified LLVM IR, and can write a host-target
object file. Explicit external functions and opaque-pointer classes are
supported through link-time ABI declarations. The C++ compiler does not execute
generated code in-process.

## Install and run Python examples

The Python CLI requires Python 3.10 or later and has no runtime dependencies.

```sh
python -m pip install .
hybridlang examples/c/sum.c
hybridlang --language python examples/python/sum.py
hybridlang --check examples/javascript/sum.js
```

Run without installing:

```sh
PYTHONPATH=src python -m hybridlang examples/python/sum.py
```

## Build the C++ LLVM compiler

Requirements: CMake 3.16+, C++17, Bison 3.8+, and LLVM 18+ development
libraries with the host target enabled.

```sh
cmake -S cpp -B build/cpp
cmake --build build/cpp
ctest --test-dir build/cpp --output-on-failure
```

From `build/cpp`, `./example_emit_object` writes `hybrid-program.o`.
`./example_native_functions` and `./example_host_class` print LLVM IR. See
[the C++ guide](docs/cpp-sdk.md) for the API, grammar subset, and external ABI.

`--language auto` (the default) uses the file extension first and then a few
source markers. Pass `--language` explicitly when detection is ambiguous. Use
`--max-steps N` to tune the Python runtime execution budget.

## Project contents

- `src/hybridlang/` — Python frontends, runtime, and CLI.
- `cpp/grammars/hybrid.y` — Bison grammar for the C++ Hybrid subset.
- `cpp/include/hybridlang/jit.hpp` and `cpp/src/jit.cpp` — C++ API and LLVM
  IR/object-file backend.
- `cpp/examples/` — object-emission, native-function, and host-class samples.
- `cpp/tests/` — parser, IR, ABI, and object-emission tests.
- `examples/` — runnable Python frontend examples for the seven syntax subsets.
- `docs/manual.md` — supported syntax and implementation boundaries.
- `docs/usage.md` — CLI, Python API, and C++ build usage.
- `docs/cpp-sdk.md` — C++ LLVM API, ABI registration, and output details.
- `docs/pdf-analysis.md` — source PDF analysis and implementation decisions.

## Validate Python frontends

```sh
PYTHONPATH=src python -m unittest discover -s tests -v
```

## License

MIT. See [LICENSE](LICENSE).
