"""Validation abstraction for generated harnesses.

Concrete implementation wrapping the checker's ``check_harness`` pipeline
(detection → triage → fix).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from harness.checker.detect import check_harness


@dataclass
class ValidationResult:
    """Structured outcome of a harness validation pass."""

    is_valid: bool
    violations: list[str] = field(default_factory=list)
    feedback: str = ""
    suggested_fix: str = ""


class CheckerValidator:
    """Wraps ``harness.checker.detect.check_harness``.

    Runs the checker's 3-step pipeline (detection -> triage -> fix) against
    the generated harness source code and returns a :class:`ValidationResult`.
    """

    def __init__(
        self,
        model: str = "deepseek/deepseek-chat",
        *,
        project: str | None = None,
        fuzzer_name: str | None = None,
    ) -> None:
        self.model = model
        self._project = project
        self._fuzzer_name = fuzzer_name

    async def validate(
        self,
        harness_code: str,
        fn_name: str,
        project_path: str,
    ) -> ValidationResult:
        project_name = self._project or Path(project_path).name
        fuzzer = self._fuzzer_name or fn_name

        result = await check_harness(
            source_code=harness_code,
            project=project_name,
            fuzzer=fuzzer,
            model=self.model,
            generate_fix=True,
        )

        if not result.needs_fix:
            return ValidationResult(
                is_valid=True,
                violations=result.principles,
                feedback=result.detection_output,
            )

        return ValidationResult(
            is_valid=False,
            violations=result.principles,
            feedback=result.detection_output,
            suggested_fix=result.fix_code,
        )
