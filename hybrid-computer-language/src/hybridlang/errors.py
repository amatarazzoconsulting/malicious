class CompileError(Exception):
    """Raised when source text is outside the supported language subset."""


class ExecutionError(Exception):
    """Raised when a compiled program fails during execution."""


class RuntimeLimitError(ExecutionError):
    """Raised when a program exceeds its instruction budget."""
