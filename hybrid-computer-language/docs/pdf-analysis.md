# Source PDF analysis

## Materials reviewed

- **Hybrid Computer Language Parser - Proton Docs.pdf**
- **JIT Engine Interface - Proton Docs.pdf**

The PDFs describe a proposed multi-language pipeline with lexical analysis,
recursive-descent parsing, AST construction, semantic checks, CPU targets,
optimization levels, native code generation, and object-oriented features.
The parser PDF names C, C++, Python, Go, BASIC, COBOL, and JavaScript. The
documents also discuss API callbacks, primitive/container types, diagnostics,
and security policy.

## Findings and implementation decisions

The repository supplied for analysis had the PDFs but no parser, compiler, or
JIT source code to extract. The PDFs are design narratives and API sketches,
not executable source or a complete formal grammar. Their stated feature set
cannot be implemented faithfully just by rearranging files.

This project starts a separate implementation rather than pretending that
missing code was recovered. It keeps the useful architectural idea—a set of
frontends lowered to a shared representation—and makes the limits explicit:

1. Seven small, documented Python frontends accept syntax subsets and lower to a
   shared statement model.
2. The Python runtime validates expressions and compiles them to CPython
   bytecode functions.
3. A C++17 SDK compiles a separate Hybrid C-like syntax to verified portable
   bytecode binaries and runs them in a bounded VM.
4. C++ applications may explicitly register native function callbacks and
   selected methods/fields of host classes. The runtime does not perform
   arbitrary symbol lookup.
5. Native CPU code generation, source-defined classes, inheritance/vtables,
   full language compatibility, and the PDFs' broader APIs remain out of scope.

The manuals and examples describe the implementation that actually exists;
they do not repeat unverified performance or compatibility claims from the PDFs.
