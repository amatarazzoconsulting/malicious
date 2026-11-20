from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .compiler import compile_source
from .errors import CompileError, ExecutionError
from .frontends import LANGUAGES


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="hybridlang",
        description="Compile and run the supported Hybrid language subsets.",
    )
    parser.add_argument("source", type=Path, help="source file to compile")
    parser.add_argument(
        "--language",
        choices=("auto", *LANGUAGES),
        default="auto",
        help="source syntax (default: infer from file extension/content)",
    )
    parser.add_argument(
        "--max-steps",
        type=int,
        default=100_000,
        help="maximum executed statements and expressions (default: 100000)",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="compile and validate without running",
    )
    args = parser.parse_args(argv)

    try:
        source_text = args.source.read_text(encoding="utf-8")
        program = compile_source(source_text, args.language, str(args.source))
        if args.check:
            print(
                f"OK: {args.source} ({program.language}, "
                f"{program.compiled_expression_count} compiled expressions)"
            )
            return 0
        sys.stdout.write(program.run(args.max_steps))
        return 0
    except (OSError, CompileError, ExecutionError, ValueError) as exc:
        print(f"hybridlang: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
