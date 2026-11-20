from __future__ import annotations

import ast
import math
import re

from .errors import CompileError


def parse_expression(text: str, line: int) -> ast.expr:
    text = text.strip().rstrip(".")
    text = re.sub(r"\btrue\b", "True", text, flags=re.IGNORECASE)
    text = re.sub(r"\bfalse\b", "False", text, flags=re.IGNORECASE)
    text = text.replace("&&", " and ").replace("||", " or ")
    text = re.sub(r"!(?!=)", " not ", text)
    text = text.strip()

    try:
        expression = ast.parse(text, mode="eval").body
    except SyntaxError as exc:
        raise CompileError(f"line {line}: invalid expression: {exc.msg}") from exc

    _validate(expression, line)
    return expression


def _validate(node: ast.AST, line: int) -> None:
    if isinstance(node, ast.Constant):
        if isinstance(node.value, (bool, int, float, str)):
            if isinstance(node.value, int) and not isinstance(node.value, bool):
                if node.value.bit_length() > 4096:
                    raise CompileError(f"line {line}: integer literal exceeds the 4096-bit limit")
            elif isinstance(node.value, float) and not math.isfinite(node.value):
                raise CompileError(f"line {line}: floating-point literal must be finite")
            elif isinstance(node.value, str) and len(node.value) > 100_000:
                raise CompileError(f"line {line}: string literal exceeds the 100000-character limit")
            return
        raise CompileError(f"line {line}: unsupported literal")
    if isinstance(node, ast.Name):
        return
    if isinstance(node, ast.BinOp) and isinstance(
        node.op,
        (
            ast.Add,
            ast.Sub,
            ast.Mult,
            ast.Div,
            ast.FloorDiv,
            ast.Mod,
            ast.BitAnd,
            ast.BitOr,
            ast.BitXor,
            ast.LShift,
            ast.RShift,
        ),
    ):
        _validate(node.left, line)
        _validate(node.right, line)
        return
    if isinstance(node, ast.UnaryOp) and isinstance(
        node.op, (ast.UAdd, ast.USub, ast.Not, ast.Invert)
    ):
        _validate(node.operand, line)
        return
    if isinstance(node, ast.BoolOp) and isinstance(node.op, (ast.And, ast.Or)):
        for value in node.values:
            _validate(value, line)
        return
    if isinstance(node, ast.Compare) and all(
        isinstance(op, (ast.Eq, ast.NotEq, ast.Lt, ast.LtE, ast.Gt, ast.GtE))
        for op in node.ops
    ):
        _validate(node.left, line)
        for comparator in node.comparators:
            _validate(comparator, line)
        return
    raise CompileError(
        f"line {line}: unsupported expression construct: {type(node).__name__}"
    )
