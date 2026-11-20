from __future__ import annotations

import ast
import re
from dataclasses import dataclass

from .errors import CompileError
from .expressions import parse_expression
from .nodes import Assign, If, Print, Statement, While


LANGUAGES = ("c", "cpp", "python", "go", "basic", "cobol", "javascript")


@dataclass(frozen=True)
class Token:
    value: str
    line: int
    column: int


_TOKEN_PATTERN = re.compile(
    r"(?P<space>[ \t\r]+)"
    r"|(?P<comment>//[^\n]*|/\*[\s\S]*?\*/|#[^\n]*)"
    r"|(?P<number>(?:\d+\.\d+|\d+))"
    r"|(?P<string>'(?:\\.|[^'\\])*'|\"(?:\\.|[^\"\\])*\")"
    r"|(?P<identifier>[A-Za-z_][A-Za-z_0-9]*)"
    r"|(?P<operator>::|\+\+|--|\+=|-=|\*=|/=|%=|:=|==|!=|<=|>=|&&|\|\||<<|>>)"
    r"|(?P<symbol>[{}();,.=+\-*/%&|^~!<>])"
    r"|(?P<newline>\n)",
)


def detect_language(source: str, filename: str | None = None) -> str:
    if filename:
        suffix = filename.rsplit(".", 1)[-1].lower() if "." in filename else ""
        by_extension = {
            "c": "c",
            "h": "c",
            "cc": "cpp",
            "cpp": "cpp",
            "cxx": "cpp",
            "hpp": "cpp",
            "py": "python",
            "go": "go",
            "bas": "basic",
            "basic": "basic",
            "cob": "cobol",
            "cbl": "cobol",
            "js": "javascript",
            "mjs": "javascript",
        }
        if suffix in by_extension:
            return by_extension[suffix]

    checks = (
        ("cobol", r"(?im)^\s*(IDENTIFICATION DIVISION|PROGRAM-ID|DISPLAY|MOVE)\b"),
        ("basic", r"(?im)^\s*(LET|PRINT|WEND|END IF)\b"),
        ("go", r"(?m)^\s*(package\s+\w+|func\s+main\s*\()"),
        ("python", r"(?m)^\s*(def\s+\w+|elif\b|print\s*\()"),
        ("javascript", r"\b(console\.log|const\s+\w+\s*=|let\s+\w+\s*=)"),
        ("cpp", r"(?m)^\s*#include\b|std::"),
        ("c", r"\bint\s+main\s*\("),
    )
    for language, pattern in checks:
        if re.search(pattern, source):
            return language
    return "c"


def parse_source(source: str, language: str) -> tuple[Statement, ...]:
    if language not in LANGUAGES:
        raise CompileError(
            f"unsupported language {language!r}; choose from {', '.join(LANGUAGES)}"
        )
    if language == "python":
        return _parse_python(source)
    if language == "basic":
        source = _translate_basic(source)
    elif language == "cobol":
        source = _translate_cobol(source)
    elif language == "go":
        source = _translate_go_header(source)

    parser = _Parser(_tokenize(source))
    return tuple(parser.parse_program())


def _tokenize(source: str) -> list[Token]:
    tokens: list[Token] = []
    line = 1
    column = 1
    offset = 0
    while offset < len(source):
        match = _TOKEN_PATTERN.match(source, offset)
        if not match:
            excerpt = source[offset : offset + 12].splitlines()[0]
            raise CompileError(f"line {line}, column {column}: unexpected text {excerpt!r}")
        value = match.group()
        kind = match.lastgroup
        if kind not in ("space", "comment", "newline"):
            tokens.append(Token(value, line, column))
        if "\n" in value:
            line += value.count("\n")
            column = len(value.rsplit("\n", 1)[-1]) + 1
        else:
            column += len(value)
        offset = match.end()
    tokens.append(Token("<eof>", line, column))
    return tokens


class _Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.position = 0

    def parse_program(self) -> list[Statement]:
        statements: list[Statement] = []
        while not self._at("<eof>"):
            if self._accept(";"):
                continue
            if self._at("}") and self.position == 0:
                self._error("unexpected closing brace")
            statement = self._statement()
            if isinstance(statement, _Block):
                statements.extend(statement.statements)
            elif not isinstance(statement, _Ignored):
                statements.append(statement)
        return statements

    def _statement(self) -> Statement | _Block | _Ignored:
        token = self._current()
        lower = token.value.lower()
        if lower in ("package", "import", "using"):
            self._skip_to_semicolon()
            return _Ignored()
        if lower == "func" or (
            lower in ("int", "void", "float", "double", "long", "static")
            and self._looks_like_function()
        ):
            return self._function()
        if self._accept("{"):
            return _Block(tuple(self._statements_until("}")))
        if lower == "if":
            self._advance()
            condition = self._parenthesized_expression()
            body = self._as_body(self._statement())
            otherwise: tuple[Statement, ...] = ()
            if self._at_word("else"):
                self._advance()
                otherwise = self._as_body(self._statement())
            return If(condition, body, otherwise, token.line)
        if lower == "while":
            self._advance()
            condition = self._parenthesized_expression()
            body = self._as_body(self._statement())
            return While(condition, body, token.line)
        if lower in ("print", "println", "console", "fmt", "printf"):
            call = self._print_call()
            return call
        if lower == "std" and self.position + 2 < len(self.tokens) and self.tokens[self.position + 1].value == "::" and self.tokens[self.position + 2].value == "cout":
            return self._stream_print()
        if lower in ("let", "var", "const", "int", "long", "float", "double", "bool", "string", "auto", "char"):
            return self._declaration()
        if token.value == "<eof>" or token.value == "}":
            self._error("expected a statement")
        return self._assignment()

    def _statements_until(self, terminator: str) -> list[Statement]:
        statements: list[Statement] = []
        while not self._at(terminator) and not self._at("<eof>"):
            if self._accept(";"):
                continue
            statement = self._statement()
            if not isinstance(statement, _Ignored):
                if isinstance(statement, _Block):
                    statements.extend(statement.statements)
                else:
                    statements.append(statement)
        self._expect(terminator)
        return statements

    def _function(self) -> _Block:
        start = self._current()
        if self._accept_word("func"):
            name = self._expect_identifier().value
        else:
            name = ""
            while not self._at("(") and not self._at("<eof>"):
                name = self._advance().value
        self._expect("(")
        self._expect(")")
        if name != "main":
            self._error("only a zero-argument main function is supported", start)
        body = self._statement()
        if not isinstance(body, _Block):
            self._error("main must have a braced body", start)
        return body

    def _looks_like_function(self) -> bool:
        pos = self.position
        while pos < len(self.tokens) and self.tokens[pos].value.lower() in (
            "static",
            "long",
        ):
            pos += 1
        return (
            pos + 2 < len(self.tokens)
            and re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", self.tokens[pos + 1].value)
            is not None
            and self.tokens[pos + 2].value == "("
        )

    def _declaration(self) -> Assign:
        start = self._advance()
        name = self._expect_identifier().value
        if start.value.lower() == "var" and self._current().value not in ("=", ";"):
            self._advance()
        if self._accept("=") or self._accept(":="):
            expression = self._expression_until_semicolon()
        else:
            expression = ast.Constant(value=0)
            self._expect(";")
        return Assign(name, expression, start.line)

    def _assignment(self) -> Assign:
        start = self._expect_identifier()
        name = start.value
        operator = self._advance().value
        if operator not in ("=", ":=", "+=", "-=", "*=", "/=", "%="):
            self._error("expected an assignment operator", start)
        right = self._expression_until_semicolon()
        if operator in ("+=", "-=", "*=", "/=", "%="):
            op_type = {
                "+=": ast.Add,
                "-=": ast.Sub,
                "*=": ast.Mult,
                "/=": ast.Div,
                "%=": ast.Mod,
            }[operator]
            left = ast.Name(id=name, ctx=ast.Load())
            right = ast.BinOp(left=left, op=op_type(), right=right)
        return Assign(name, right, start.line)

    def _print_call(self) -> Print:
        start = self._current()
        name = self._advance().value.lower()
        if name in ("console", "fmt"):
            self._expect(".")
            member = self._expect_identifier().value.lower()
            if (name, member) not in (("console", "log"), ("fmt", "println")):
                self._error("only console.log and fmt.Println are supported", start)
        elif name == "printf":
            self._expect("(")
            expressions = self._call_arguments()
            self._expect(";")
            if not expressions or not isinstance(expressions[0], ast.Constant) or not isinstance(expressions[0].value, str):
                self._error("printf requires a string-literal format as its first argument", start)
            return Print(tuple(expressions), start.line, "printf")
        elif name not in ("print", "println"):
            self._error("unsupported output function", start)
        self._expect("(")
        expressions = self._call_arguments()
        self._expect(";")
        return Print(tuple(expressions), start.line)

    def _stream_print(self) -> Print:
        start = self._advance()
        self._expect("::")
        self._expect("cout")
        expressions: list[ast.expr] = []
        newline = False
        while self._accept("<<"):
            if self._at("std") and self.position + 2 < len(self.tokens) and self.tokens[self.position + 1].value == "::" and self.tokens[self.position + 2].value == "endl":
                self._advance()
                self._advance()
                self._advance()
                newline = True
                break
            expressions.append(self._expression_until({"<<", ";"}))
        self._expect(";")
        return Print(tuple(expressions), start.line, "stream-line" if newline else "stream")

    def _call_arguments(self) -> list[ast.expr]:
        expressions: list[ast.expr] = []
        if self._accept(")"):
            return expressions
        while True:
            expressions.append(self._expression_until({",", ")"}))
            if self._accept(")"):
                return expressions
            self._expect(",")

    def _parenthesized_expression(self) -> ast.expr:
        self._expect("(")
        expression = self._expression_until({")"})
        self._expect(")")
        return expression

    def _expression_until_semicolon(self) -> ast.expr:
        expression = self._expression_until({";"})
        self._expect(";")
        return expression

    def _expression_until(self, delimiters: set[str]) -> ast.expr:
        start = self._current()
        pieces: list[str] = []
        depth = 0
        while not self._at("<eof>"):
            value = self._current().value
            if depth == 0 and value in delimiters:
                break
            if value == "(":
                depth += 1
            elif value == ")":
                if depth == 0:
                    break
                depth -= 1
            pieces.append(self._advance().value)
        if not pieces:
            self._error("expected an expression", start)
        return parse_expression(" ".join(pieces), start.line)

    def _skip_to_semicolon(self) -> None:
        while not self._at("<eof>") and not self._accept(";"):
            self._advance()

    def _as_body(
        self, statement: Statement | _Block | _Ignored
    ) -> tuple[Statement, ...]:
        if isinstance(statement, _Ignored):
            return ()
        if isinstance(statement, _Block):
            return statement.statements
        return (statement,)

    def _current(self) -> Token:
        return self.tokens[self.position]

    def _advance(self) -> Token:
        token = self._current()
        self.position += 1
        return token

    def _at(self, value: str) -> bool:
        return self._current().value == value

    def _at_word(self, value: str) -> bool:
        return self._current().value.lower() == value

    def _accept(self, value: str) -> bool:
        if self._at(value):
            self._advance()
            return True
        return False

    def _accept_word(self, value: str) -> bool:
        if self._at_word(value):
            self._advance()
            return True
        return False

    def _expect(self, value: str) -> Token:
        if not self._at(value):
            self._error(f"expected {value!r}")
        return self._advance()

    def _expect_identifier(self) -> Token:
        token = self._current()
        if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", token.value) is None:
            self._error("expected an identifier", token)
        return self._advance()

    def _error(self, message: str, token: Token | None = None) -> None:
        current = token or self._current()
        raise CompileError(
            f"line {current.line}, column {current.column}: {message}"
        )


@dataclass(frozen=True)
class _Block:
    statements: tuple[Statement, ...]


@dataclass(frozen=True)
class _Ignored:
    pass


def _parse_python(source: str) -> tuple[Statement, ...]:
    try:
        module = ast.parse(source)
    except SyntaxError as exc:
        raise CompileError(f"line {exc.lineno}: {exc.msg}") from exc
    return tuple(_convert_python_statement(node) for node in module.body)


def _convert_python_statement(node: ast.stmt) -> Statement:
    line = getattr(node, "lineno", 1)
    if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(
        node.targets[0], ast.Name
    ):
        return Assign(
            node.targets[0].id,
            _python_expression(node.value, line),
            line,
        )
    if isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
        value = node.value or ast.Constant(value=0)
        return Assign(node.target.id, _python_expression(value, line), line)
    if isinstance(node, ast.AugAssign) and isinstance(node.target, ast.Name):
        op = node.op
        supported = (ast.Add, ast.Sub, ast.Mult, ast.Div, ast.Mod)
        if not isinstance(op, supported):
            raise CompileError(f"line {line}: unsupported augmented assignment")
        expression = ast.BinOp(
            left=ast.Name(id=node.target.id, ctx=ast.Load()),
            op=op,
            right=_python_expression(node.value, line),
        )
        return Assign(node.target.id, _python_expression(expression, line), line)
    if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call):
        call = node.value
        if isinstance(call.func, ast.Name) and call.func.id == "print" and not call.keywords:
            return Print(tuple(_python_expression(arg, line) for arg in call.args), line)
        raise CompileError(f"line {line}: only print(...) calls are supported")
    if isinstance(node, ast.If):
        return If(
            _python_expression(node.test, line),
            tuple(_convert_python_statement(item) for item in node.body),
            tuple(_convert_python_statement(item) for item in node.orelse),
            line,
        )
    if isinstance(node, ast.While) and not node.orelse:
        return While(
            _python_expression(node.test, line),
            tuple(_convert_python_statement(item) for item in node.body),
            line,
        )
    raise CompileError(f"line {line}: unsupported statement {type(node).__name__}")


def _python_expression(node: ast.expr, line: int) -> ast.expr:
    from .expressions import _validate

    _validate(node, line)
    return node


def _translate_go_header(source: str) -> str:
    lines = []
    for raw in source.splitlines():
        line = raw.rstrip()
        if re.match(r"^\s*(package\s+\w+|import\s+(?:\([^\)]*\)|\"[^\"]+\"))\s*$", line):
            lines.append("")
            continue
        line = re.sub(r"^(\s*)for\s+(.+?)\s*\{", r"\1while (\2) {", line)
        if line.strip() and not line.strip().startswith("}") and not line.rstrip().endswith("{"):
            line += ";"
        lines.append(line)
    return "\n".join(lines)


def _translate_basic(source: str) -> str:
    output: list[str] = []
    for line_number, raw in enumerate(source.splitlines(), 1):
        line = raw.strip()
        if not line or line.upper().startswith("REM "):
            output.append("")
            continue
        upper = line.upper()
        if upper == "ELSE":
            output.append("else {")
        elif upper == "ELSEIF":
            raise CompileError(f"line {line_number}: ELSEIF is not supported")
        elif upper in ("END IF", "ENDIF", "WEND", "END WHILE"):
            output.append("}")
        elif upper.startswith("IF ") and upper.endswith(" THEN"):
            condition = line[3:-5].strip()
            condition = re.sub(r"<>", "!=", condition)
            condition = re.sub(r"(?<![<>=!])=(?!=)", "==", condition)
            output.append(f"if ({condition}) {{")
        elif upper.startswith("WHILE "):
            output.append(f"while ({line[6:].strip()}) {{")
        elif upper.startswith("LET "):
            output.append(f"let {line[4:].strip()};")
        elif upper.startswith("PRINT "):
            output.append(f"println({line[6:].strip()});")
        elif upper in ("END", "STOP"):
            output.append("")
            continue
        else:
            output.append(line.rstrip(";") + ";")
    if not output and source.strip():
        raise CompileError("line 1: no supported BASIC statements")
    return "\n".join(output)


def _translate_cobol(source: str) -> str:
    output: list[str] = []
    for line_number, raw in enumerate(source.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("*"):
            output.append("")
            continue
        line = re.sub(r"^\d{1,6}\s+", "", line)
        upper = line.upper().rstrip(".")
        if upper.startswith(("IDENTIFICATION DIVISION", "PROGRAM-ID", "DATA DIVISION", "PROCEDURE DIVISION", "WORKING-STORAGE SECTION")):
            output.append("")
            continue
        if upper == "ELSE":
            output.append("else {")
        elif upper in ("END-IF", "END-PERFORM"):
            output.append("}")
        elif upper.startswith("IF "):
            condition = line[3:].rstrip(".")
            condition = re.sub(r"\bIS\s+GREATER\s+THAN\b", ">", condition, flags=re.I)
            condition = re.sub(r"\bIS\s+LESS\s+THAN\b", "<", condition, flags=re.I)
            condition = re.sub(r"\bIS\s+NOT\s+EQUAL\s+TO\b", "!=", condition, flags=re.I)
            condition = re.sub(r"\bIS\s+EQUAL\s+TO\b", "==", condition, flags=re.I)
            condition = re.sub(r"(?<![<>=!])=(?!=)", "==", condition)
            output.append(f"if ({condition}) {{")
        elif upper.startswith("PERFORM UNTIL "):
            condition = line[len("PERFORM UNTIL ") :].rstrip(".")
            output.append(f"while (!({condition})) {{")
        elif upper.startswith("DISPLAY "):
            output.append(f"println({line[8:].rstrip('.')});")
        elif upper.startswith("MOVE "):
            match = re.match(r"MOVE\s+(.+)\s+TO\s+([A-Za-z_]\w*)\.?$", line, re.I)
            if not match:
                raise CompileError(f"line {line_number}: expected MOVE expression TO name")
            output.append(f"let {match.group(2)} = {match.group(1).rstrip('.')};")
        elif upper.startswith("COMPUTE "):
            output.append(f"{line[8:].rstrip('.')};")
        elif upper in ("STOP RUN", "GOBACK"):
            output.append("")
            continue
        else:
            raise CompileError(f"line {line_number}: unsupported COBOL statement {line!r}")
    return "\n".join(output)
