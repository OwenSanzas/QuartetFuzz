# HarnessChecker

Fuzz harness quality checking — P1-P4 violation detection, triage, and fix
generation. The core module (`detect.py`) is used by the generation pipeline
as a validation step.

## Architecture

The checker's core is `detect.py`, which implements a 3-step LLM pipeline:

1. **Detection** — scan harness source for P1-P4 violations (single LLM call)
2. **Triage** — 5 parallel majority-vote calls to filter false positives
   (only when P1/P2 detected)
3. **Fix** — generate minimal fix code for confirmed violations

```
harness/checker/
├── detect.py                    # check_harness() — detection + triage + fix pipeline
├── HARNESS_CHECKING_MANUAL.md   # manual review & submission workflow
├── tests/
│   └── test_detection.py
└── README.md
```

## Usage

The checker is primarily used programmatically through `CheckerValidator`
in the generation pipeline:

```python
from harness.validator import CheckerValidator

checker = CheckerValidator(model="deepseek/deepseek-chat")
result = await checker.validate(harness_code, fn_name, project_path)
# result.is_valid, result.violations, result.suggested_fix
```

Or directly via `check_harness()`:

```python
from harness.checker.detect import check_harness

result = await check_harness(
    source_code=harness_source,
    project="libyaml",
    fuzzer="yaml_scanner_fuzz",
    model="deepseek/deepseek-chat",
)
# result.has_violations, result.needs_fix, result.fix_code
```

## The Four Principles

| Principle | Scope |
|---|---|
| **P1** — Fuzzer Logic Must Be Correct | Bugs in the harness itself (UB, leaks, wrong values) |
| **P2** — Follow Target API Protocol | Wrong init order, create-and-discard, wrong params |
| **P3** — Respect Security Boundaries | Internal headers, private functions |
| **P4** — Use Correct Entry Points | Missing essential setup, wrong abstraction level |

Only P1 and P2 violations are actionable (generate fixes). P3 and P4 are
architectural observations reported but not auto-fixed.
