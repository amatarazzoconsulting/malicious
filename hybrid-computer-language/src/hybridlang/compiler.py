from __future__ import annotations

import ast
from io import StringIO
import re

from .errors import CompileError, ExecutionError, RuntimeLimitError
from .frontends import detect_language, parse_source
from .jit import ExpressionJIT
from .nodes import Assign, If, Print, Statement, While

_LANGUAGE_ALIASES = {
    "c++": "cpp",
    "js": "javascript",
    "py": "python",
    "cob": "cobol",
}


class CompiledProgram:
    """Validated program with expression code compiled by the JIT backend."""

    def __init__(self, statements: tuple[Statement, ...], language: str):
        self.statements = statements
        self.language = language
        self._jit = ExpressionJIT()
        for expression in _expressions(statements):
            self._jit.compile(expression)

    @property
    def compiled_expression_count(self) -> int:
        return self._jit.compiled_count

    def run(self, max_steps: int = 100_000) -> str:
        if not isinstance(max_steps, int) or isinstance(max_steps, bool) or max_steps < 1:
            raise ValueError("max_steps must be a positive integer")
        output = StringIO()
        environment: dict[str, object] = {}
        steps = 0

        def tick(line: int) -> None:
            nonlocal steps
            if steps >= max_steps:
                raise RuntimeLimitError(
                    f"line {line}: execution exceeded the {max_steps}-step limit"
                )
            steps += 1

        def evaluate(expression: ast.expr, line: int) -> object:
            tick(line)
            try:
                return self._jit.compile(expression)(environment)
            except (ArithmeticError, KeyError, TypeError, ValueError) as exc:
                raise ExecutionError(f"line {line}: {exc}") from exc

        def write_output(text: str, line: int) -> None:
            if output.tell() + len(text) > 1_000_000:
                raise ExecutionError(
                    f"line {line}: output exceeds the 1000000-character limit"
                )
            output.write(text)

        def execute(statements: tuple[Statement, ...]) -> None:
            for statement in statements:
                tick(statement.line)
                if isinstance(statement, Assign):
                    environment[statement.name] = evaluate(statement.expression, statement.line)
                elif isinstance(statement, Print):
                    values = [
                        evaluate(expression, statement.line)
                        for expression in statement.expressions
                    ]
                    if statement.mode == "printf":
                        try:
                            write_output(
                                _format_printf(values, statement.line), statement.line
                            )
                        except ExecutionError:
                            raise
                        except (ArithmeticError, TypeError, ValueError) as exc:
                            raise ExecutionError(
                                f"line {statement.line}: printf formatting failed: {exc}"
                            ) from exc
                    elif statement.mode == "stream":
                        write_output("".join(map(str, values)), statement.line)
                    elif statement.mode == "stream-line":
                        write_output("".join(map(str, values)) + "\n", statement.line)
                    else:
                        write_output(" ".join(map(str, values)) + "\n", statement.line)
                elif isinstance(statement, If):
                    branch = statement.body if evaluate(statement.condition, statement.line) else statement.otherwise
                    execute(branch)
                elif isinstance(statement, While):
                    while evaluate(statement.condition, statement.line):
                        execute(statement.body)
                else:
                    raise ExecutionError(f"line {statement.line}: invalid compiled statement")

        execute(self.statements)
        return output.getvalue()


def compile_source(
    source: str,
    language: str = "auto",
    filename: str | None = None,
) -> CompiledProgram:
    if len(source.encode("utf-8")) > 1_000_000:
        raise CompileError("source exceeds the 1 MB input limit")
    selected = language.lower()
    if selected == "auto":
        selected = detect_language(source, filename)
    selected = _LANGUAGE_ALIASES.get(selected, selected)
    statements = parse_source(source, selected)
    return CompiledProgram(statements, selected)


def _expressions(statements: tuple[Statement, ...]):
    for statement in statements:
        if isinstance(statement, Assign):
            yield statement.expression
        elif isinstance(statement, Print):
            yield from statement.expressions
        elif isinstance(statement, If):
            yield statement.condition
            yield from _expressions(statement.body)
            yield from _expressions(statement.otherwise)
        elif isinstance(statement, While):
            yield statement.condition
            yield from _expressions(statement.body)


def _format_printf(values: list[object], line: int) -> str:
    format_string = values[0]
    if not isinstance(format_string, str):
        raise ExecutionError(f"line {line}: printf format must be a string")
    arguments = iter(values[1:])
    pattern = re.compile(r"%(?:%|[-+0-9.]*[diuxXfsc])")

    position = 0
    for match in pattern.finditer(format_string):
        if "%" in format_string[position : match.start()]:
            raise ExecutionError(f"line {line}: unsupported printf format specifier")
        position = match.end()
    if "%" in format_string[position:]:
        raise ExecutionError(f"line {line}: unsupported printf format specifier")

    def replace(match: re.Match[str]) -> str:
        specifier = match.group()
        if specifier == "%%":
            return "%"
        try:
            value = next(arguments)
        except StopIteration as exc:
            raise ExecutionError(f"line {line}: printf has too few arguments") from exc
        code = specifier[-1]
        if code in "diu":
            return str(int(value))
        if code in "xX":
            return format(int(value), code)
        if code == "f":
            precision_match = re.search(r"\.(\d+)", specifier)
            precision = int(precision_match.group(1)) if precision_match else 6
            if precision > 1000:
                raise ExecutionError(
                    f"line {line}: printf precision exceeds the 1000-digit limit"
                )
            return f"{float(value):.{precision}f}"
        if code == "c":
            return chr(int(value))
        if code == "s":
            return str(value)
        return " "

    formatted = pattern.sub(replace, format_string)
    try:
        next(arguments)
    except StopIteration:
        pass
    else:
        raise ExecutionError(f"line {line}: printf has too many arguments")
    return formatted
