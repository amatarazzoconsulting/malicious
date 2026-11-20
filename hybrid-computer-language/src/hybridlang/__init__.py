"""A small, multi-syntax language compiler and expression JIT."""

from .compiler import CompiledProgram, compile_source
from .errors import CompileError, ExecutionError, RuntimeLimitError

__all__ = [
    "CompiledProgram",
    "CompileError",
    "ExecutionError",
    "RuntimeLimitError",
    "compile_source",
]
