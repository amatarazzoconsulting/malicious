# Hybrid Computer Language

A small, dependency-free compiler prototype inspired by the **Hybrid Computer
Language Parser** and **JIT Engine Interface** PDFs. It demonstrates how several
source syntaxes can lower to one validated execution model.

> **Scope:** this is an educational subset compiler, not a C/C++/Python/Go/BASIC/
> COBOL/JavaScript implementation. Each frontend supports only the constructs in
> [the language manual](docs/manual.md). It does not emit native machine code.

The compiler parses source into a restricted AST, compiles each validated
expression to CPython bytecode, then executes statements under an instruction
budget. User input is never passed directly to Python `eval` or `exec`; the
expression compiler accepts only literals, variable reads, arithmetic, boolean,
comparison, and bitwise operators. The runtime has no filesystem, network, or
process APIs.

## Install

Requires Python 3.10 or later; there are no runtime dependencies.

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

`--language auto` (the default) uses the file extension first and then a few
source markers. Pass `--language` explicitly when detection is ambiguous. Use
`--max-steps N` to tune the execution budget; loops stop with an error when the
limit is exceeded.

## Project contents

- `src/hybridlang/` — language frontends, AST validation, expression JIT, and CLI.
- `examples/` — runnable examples for the seven syntax subsets.
- `tests/` — focused compiler, runtime, diagnostic, and budget tests.
- `docs/manual.md` — supported syntax and implementation boundaries.
- `docs/usage.md` — CLI and Python API usage.
- `docs/pdf-analysis.md` — analysis of the source PDFs and scope decisions.

## Validate

```sh
PYTHONPATH=src python -m unittest discover -s tests -v
```

## License

MIT. See [LICENSE](LICENSE).
