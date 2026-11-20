from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "src"))

from hybridlang import CompileError, ExecutionError, RuntimeLimitError, compile_source


ROOT = Path(__file__).parents[1]


class CompilerTests(unittest.TestCase):
    def test_examples_compile_and_run(self) -> None:
        expected = {
            "c": "sum=15\n",
            "cpp": "sum=15\n",
            "python": "sum= 15\n",
            "go": "sum= 15\n",
            "basic": "sum= 15\n",
            "cobol": "15\n",
            "javascript": "sum= 15\n",
        }
        for language, output in expected.items():
            with self.subTest(language=language):
                paths = list((ROOT / "examples" / language).iterdir())
                program = compile_source(
                    paths[0].read_text(encoding="utf-8"),
                    filename=str(paths[0]),
                )
                self.assertEqual(program.language, language)
                self.assertEqual(program.run(), output)
                self.assertGreater(program.compiled_expression_count, 0)

    def test_python_api_and_operator_precedence(self) -> None:
        program = compile_source(
            "answer = 2 + 3 * 4\nprint(answer)\n", language="python"
        )
        self.assertEqual(program.run(), "14\n")

    def test_printf_escaped_percent_and_formatting(self) -> None:
        program = compile_source(
            'int main() { printf("progress=100%% value=%d\\n", 7); }',
            language="c",
        )
        self.assertEqual(program.run(), "progress=100% value=7\n")

    def test_rejects_host_language_calls_and_attributes(self) -> None:
        for source in (
            'print(__import__("os"))\n',
            "print(object.__class__)\n",
        ):
            with self.subTest(source=source):
                with self.assertRaises(CompileError):
                    compile_source(source, language="python")

    def test_execution_budget_stops_infinite_loop(self) -> None:
        program = compile_source("while (true) {}", language="c")
        with self.assertRaises(RuntimeLimitError):
            program.run(max_steps=10)

    def test_jit_bounds_expensive_expression_results(self) -> None:
        for expression, message in (
            ('"x" * 1000000000', "numeric operands"),
            ("1 << 1000000000", "shift count"),
        ):
            with self.subTest(expression=expression):
                program = compile_source(f"print({expression})\n", language="python")
                with self.assertRaisesRegex(ExecutionError, message):
                    program.run()

    def test_runtime_errors_have_source_location(self) -> None:
        program = compile_source("x = 1 / 0\n", language="python")
        with self.assertRaisesRegex(ExecutionError, "line 1"):
            program.run()

    def test_multiline_comments_preserve_source_lines(self) -> None:
        source = "/* first line\n second line */\nint main() { int x = 1 / 0; }"
        program = compile_source(source, language="c")
        with self.assertRaisesRegex(ExecutionError, "line 3"):
            program.run()

    def test_undefined_variables_are_runtime_errors(self) -> None:
        program = compile_source("print(missing)\n", language="python")
        with self.assertRaisesRegex(ExecutionError, "missing"):
            program.run()

    def test_rejects_unsupported_python_statement(self) -> None:
        with self.assertRaisesRegex(CompileError, "unsupported statement"):
            compile_source("import os\n", language="python")

    def test_rejects_unknown_language(self) -> None:
        with self.assertRaisesRegex(CompileError, "unsupported language"):
            compile_source("x = 1", language="ruby")


if __name__ == "__main__":
    unittest.main()
