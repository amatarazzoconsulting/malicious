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

1. Seven small, documented syntax subsets lower to one internal statement model.
2. Expressions are validated against a whitelist and compiled to CPython
   bytecode functions.
3. Statements execute in a small runtime with a step budget and no host APIs.
4. Native x86-64/ARM64/RISC-V code generation, classes/vtables, full language
   compatibility, and the PDF's broader APIs remain out of scope.

The manuals and examples describe the implementation that actually exists;
they do not repeat unverified performance or compatibility claims from the PDFs.
