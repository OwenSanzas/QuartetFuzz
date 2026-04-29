from __future__ import annotations

import re
from pathlib import Path

_CPP_EXTENSIONS = frozenset({".cc", ".cpp", ".cxx"})
_CPP_FEATURE_RE = re.compile(r"\bstd::|#include\s*<(?:vector|string|map|memory)>|\btemplate\s*<|\bclass\s+\w+")
_CPP_LIBFUZZER_ENTRYPOINT_RE = re.compile(
    r'(?m)^(\s*)(?!extern\s+"C"\s+)(int\s+LLVMFuzzer(?:Initialize|TestOneInput)\s*\()'
)
_C_LIBFUZZER_DEFINITION_RE = re.compile(
    r"(?m)^(?P<signature>\s*(?:int|void|size_t)\s+"
    r"(?P<name>LLVMFuzzer(?:Initialize|TestOneInput|Cleanup|CustomMutator|CustomCrossOver))\s*\([^;{]*\))\s*\{"
)
_INCLUDE_LINE_RE = re.compile(r"(?m)^#include[^\n]*\n")


def _ensure_cpp_libfuzzer_c_linkage(source: str) -> str:
    return _CPP_LIBFUZZER_ENTRYPOINT_RE.sub(r'\1extern "C" \2', source)


def _has_prior_c_prototype(source: str, name: str, definition_start: int) -> bool:
    prior_source = source[:definition_start]
    return (
        re.search(
            rf"(?m)^\s*(?:int|void|size_t)\s+{re.escape(name)}\s*\([^;{{}}]*\)\s*;",
            prior_source,
        )
        is not None
    )


def _prototype_insertion_offset(source: str) -> int:
    include_matches = list(_INCLUDE_LINE_RE.finditer(source))
    if not include_matches:
        return 0

    insert_offset = include_matches[-1].end()
    while insert_offset < len(source) and source[insert_offset] == "\n":
        insert_offset += 1
    return insert_offset


def _add_missing_c_libfuzzer_prototypes(source: str) -> str:
    missing_prototypes: list[str] = []
    for match in _C_LIBFUZZER_DEFINITION_RE.finditer(source):
        name = match.group("name")
        if _has_prior_c_prototype(source, name, match.start()):
            continue
        prototype = f"{match.group('signature').strip()};"
        if prototype not in missing_prototypes:
            missing_prototypes.append(prototype)

    if not missing_prototypes:
        return source

    insertion_offset = _prototype_insertion_offset(source)
    prototype_block = "\n".join(missing_prototypes) + "\n\n"
    return source[:insertion_offset] + prototype_block + source[insertion_offset:]


def sanitize_harness_source(
    source: str,
    source_file: str,
    *,
    force_c_linkage: bool = False,
    language: str = "c",
) -> tuple[str, str]:
    """Clean generated harness source for compilation.

    Returns (cleaned_source, corrected_extension).
    Strips `extern "C"` from .c files since LLVMFuzzerTestOneInput
    already has C linkage from the fuzzer runtime.

    For non-C/C++ languages (JS, Python, etc.), returns source unchanged.
    """
    if language not in ("c", "c++", "cpp", "cc", "cxx"):
        ext = Path(source_file).suffix or f".{language}"
        return source, ext

    ext = Path(source_file).suffix or ".c"
    cleaned = source

    if ext == ".c":
        cleaned = re.sub(r'\bextern\s+"C"\s*\{?\s*\n?', "", cleaned)
        cleaned = re.sub(r'\bextern\s+"C"\s+(?=int\s+LLVMFuzzer)', "", cleaned)
        cleaned = _add_missing_c_libfuzzer_prototypes(cleaned)
    elif ext in _CPP_EXTENSIONS:
        if force_c_linkage:
            cleaned = _ensure_cpp_libfuzzer_c_linkage(cleaned)
        elif 'extern "C"' not in cleaned and not _CPP_FEATURE_RE.search(cleaned):
            cleaned = _ensure_cpp_libfuzzer_c_linkage(cleaned)

    return cleaned, ext
