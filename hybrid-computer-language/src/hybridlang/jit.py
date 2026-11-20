from __future__ import annotations

import ast
import copy
import math
import operator
from collections.abc import Callable

from .errors import CompileError


class _EnvironmentNames(ast.NodeTransformer):
    def visit_Name(self, node: ast.Name) -> ast.expr:
        return ast.copy_location(
            ast.Subscript(
                value=ast.Name(id="env", ctx=ast.Load()),
                slice=ast.Constant(value=node.id),
                ctx=ast.Load(),
            ),
            node,
        )

    def visit_BinOp(self, node: ast.BinOp) -> ast.expr:
        operators = {
            ast.Add: "add",
            ast.Sub: "sub",
            ast.Mult: "mul",
            ast.Div: "div",
            ast.FloorDiv: "floordiv",
            ast.Mod: "mod",
            ast.BitAnd: "and",
            ast.BitOr: "or",
            ast.BitXor: "xor",
            ast.LShift: "lshift",
            ast.RShift: "rshift",
        }
        name = operators[type(node.op)]
        return ast.copy_location(
            ast.Call(
                func=ast.Name(id="_binary", ctx=ast.Load()),
                args=[
                    ast.Constant(value=name),
                    self.visit(node.left),
                    self.visit(node.right),
                ],
                keywords=[],
            ),
            node,
        )

    def visit_UnaryOp(self, node: ast.UnaryOp) -> ast.expr:
        operators = {
            ast.UAdd: "pos",
            ast.USub: "neg",
            ast.Not: "not",
            ast.Invert: "invert",
        }
        return ast.copy_location(
            ast.Call(
                func=ast.Name(id="_unary", ctx=ast.Load()),
                args=[
                    ast.Constant(value=operators[type(node.op)]),
                    self.visit(node.operand),
                ],
                keywords=[],
            ),
            node,
        )


_BINARY_OPERATORS = {
    "add": operator.add,
    "sub": operator.sub,
    "mul": operator.mul,
    "div": operator.truediv,
    "floordiv": operator.floordiv,
    "mod": operator.mod,
    "and": operator.and_,
    "or": operator.or_,
    "xor": operator.xor,
    "lshift": operator.lshift,
    "rshift": operator.rshift,
}
_UNARY_OPERATORS = {
    "pos": operator.pos,
    "neg": operator.neg,
    "not": operator.not_,
    "invert": operator.invert,
}


def _checked_value(value: object) -> object:
    if isinstance(value, int) and not isinstance(value, bool) and value.bit_length() > 4096:
        raise ValueError("integer result exceeds the 4096-bit limit")
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError("non-finite floating-point result")
    if isinstance(value, str) and len(value) > 100_000:
        raise ValueError("string result exceeds the 100000-character limit")
    return value


def _safe_binary(name: str, left: object, right: object) -> object:
    if name == "add" and isinstance(left, str) and isinstance(right, str):
        if len(left) + len(right) > 100_000:
            raise ValueError("string result exceeds the 100000-character limit")
    elif name in ("and", "or", "xor", "lshift", "rshift"):
        if not isinstance(left, int) or not isinstance(right, int):
            raise TypeError("bitwise operators require integer operands")
        if name in ("lshift", "rshift") and not 0 <= right <= 4096:
            raise ValueError("shift count must be between 0 and 4096")
    elif not isinstance(left, (int, float)) or not isinstance(right, (int, float)):
        raise TypeError("arithmetic operators require numeric operands")

    result = _BINARY_OPERATORS[name](left, right)
    return _checked_value(result)


def _safe_unary(name: str, value: object) -> object:
    if name == "invert" and not isinstance(value, int):
        raise TypeError("bitwise NOT requires an integer operand")
    if name != "not" and not isinstance(value, (int, float)):
        raise TypeError("numeric unary operators require a numeric operand")
    return _checked_value(_UNARY_OPERATORS[name](value))


class ExpressionJIT:
    """Compile validated expressions into restricted CPython bytecode functions."""

    def __init__(self) -> None:
        self._functions: dict[str, Callable[[dict[str, object]], object]] = {}

    @property
    def compiled_count(self) -> int:
        return len(self._functions)

    def compile(self, expression: ast.expr) -> Callable[[dict[str, object]], object]:
        key = ast.dump(expression, include_attributes=False)
        if key in self._functions:
            return self._functions[key]

        body = _EnvironmentNames().visit(ast.fix_missing_locations(copy.deepcopy(expression)))
        function = ast.Lambda(
            args=ast.arguments(
                posonlyargs=[],
                args=[ast.arg(arg="env")],
                vararg=None,
                kwonlyargs=[],
                kw_defaults=[],
                kwarg=None,
                defaults=[],
            ),
            body=body,
        )
        module = ast.Expression(body=function)
        ast.fix_missing_locations(module)
        try:
            code = compile(module, "<hybridlang-jit>", "eval")
            compiled = eval(
                code,
                {
                    "__builtins__": {},
                    "_binary": _safe_binary,
                    "_unary": _safe_unary,
                },
            )
        except (SyntaxError, TypeError, ValueError) as exc:
            raise CompileError(f"could not compile validated expression: {exc}") from exc
        self._functions[key] = compiled
        return compiled
