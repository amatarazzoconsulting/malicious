from __future__ import annotations

import ast
from dataclasses import dataclass


@dataclass(frozen=True)
class Assign:
    name: str
    expression: ast.expr
    line: int


@dataclass(frozen=True)
class Print:
    expressions: tuple[ast.expr, ...]
    line: int
    mode: str = "line"


@dataclass(frozen=True)
class If:
    condition: ast.expr
    body: tuple[Statement, ...]
    otherwise: tuple[Statement, ...]
    line: int


@dataclass(frozen=True)
class While:
    condition: ast.expr
    body: tuple[Statement, ...]
    line: int


Statement = Assign | Print | If | While
