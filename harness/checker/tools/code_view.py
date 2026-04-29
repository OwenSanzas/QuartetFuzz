"""Filesystem-reading MCP tools.

``read_file`` / ``list_directory`` / ``search_files`` — the same sandboxed
trio that :mod:`harness.generator.generation_agent` registers, ported here
so any BaseAgent subclass can pick them up via :func:`register`.  Paths
are resolved against ``ctx.allowed_roots`` to keep the agent inside the
project source tree (plus the optional oss-fuzz project dir).

Not modified vs. the generator's copy except:
    * ``allowed_roots`` comes from the CheckerContext
    * default search glob is ``*.c`` (same as generator)
    * registered as a module-level ``register(mcp, ctx)`` function
"""

from __future__ import annotations

import logging
import subprocess
from pathlib import Path

from mcp.server.fastmcp import FastMCP

from harness.checker.tools.context import CheckerContext

logger = logging.getLogger(__name__)


def register(mcp: FastMCP, ctx: CheckerContext) -> None:
    """Mount the three code_view tools onto *mcp*."""
    allowed_roots = [Path(p).resolve() for p in ctx.allowed_roots]
    project_path = ctx.project_path

    def _resolve_and_check(
        raw_path: str, *, is_dir: bool = False
    ) -> tuple[Path | None, str | None]:
        p = Path(raw_path)
        if not p.is_absolute():
            p = Path(project_path) / p
        try:
            resolved = p.resolve()
        except (OSError, ValueError):
            kind = "directory" if is_dir else "file"
            return None, f"Error: Invalid path: {raw_path} (expected {kind})"
        if not any(resolved == root or root in resolved.parents for root in allowed_roots):
            return None, f"Error: Access denied — path is outside the project: {raw_path}"
        return resolved, None

    @mcp.tool()
    def read_file(file_path: str, offset: int = 0, limit: int = 2000):
        """Read a file from the project source tree.

        Use this to read function definitions, headers, existing fuzzers,
        tests, or callers.  Returns content with 1-based line numbers.

        Args:
            file_path: Absolute path or path relative to the project root.
            offset: Zero-based line to start reading from.
            limit: Max number of lines to return.
        """
        p, err = _resolve_and_check(file_path)
        if err:
            return err
        if not p.exists():
            return f"Error: File not found: {file_path}"
        if not p.is_file():
            return f"Error: Not a file: {file_path}"

        with open(p, errors="replace") as f:
            lines = f.readlines()

        total = len(lines)
        selected = lines[offset : offset + limit]
        result_lines: list[str] = []
        for i, line in enumerate(selected, start=offset + 1):
            truncated = line.rstrip("\n")[:2000]
            result_lines.append(f"{i:>6}\t{truncated}")
        header = f"[{total} lines total, showing {offset + 1}-{offset + len(selected)}]"
        return header + "\n" + "\n".join(result_lines)

    @mcp.tool()
    def list_directory(dir_path: str):
        """List files and directories at the given path.

        Use this to explore project layout before searching inside
        specific subdirectories.
        """
        p, err = _resolve_and_check(dir_path, is_dir=True)
        if err:
            return err
        if not p.exists():
            return f"Error: Directory not found: {dir_path}"
        if not p.is_dir():
            return f"Error: Not a directory: {dir_path}"

        entries = sorted(p.iterdir(), key=lambda x: (x.is_file(), x.name))
        output_lines: list[str] = []
        for entry in entries:
            suffix = "/" if entry.is_dir() else ""
            size = ""
            if entry.is_file():
                try:
                    s = entry.stat().st_size
                    size = f" ({s} bytes)"
                except OSError:
                    pass
            output_lines.append(f"  {entry.name}{suffix}{size}")
        return f"{p}/ ({len(entries)} entries)\n" + "\n".join(output_lines)

    @mcp.tool()
    def search_files(directory: str, pattern: str, file_glob: str = "*.c"):
        """Search a directory for a regex pattern using ripgrep.

        Use this to find function definitions, callers, header
        declarations, or usage examples across the project.

        Args:
            directory: Absolute path or path relative to the project root.
            pattern: Regex pattern (ripgrep flavor).
            file_glob: Filename glob to restrict the search.  Default ``*.c``.
        """
        d, err = _resolve_and_check(directory, is_dir=True)
        if err:
            return err
        try:
            cmd = ["rg", "-n", "--glob", file_glob, pattern, str(d)]
            result = subprocess.run(
                cmd, capture_output=True, text=True, timeout=30
            )
            output = result.stdout.strip()
            if not output:
                return f"No matches found for '{pattern}' in {d} (glob: {file_glob})"
            lines = output.split("\n")
            if len(lines) > 100:
                return "\n".join(lines[:100]) + f"\n\n[... {len(lines) - 100} more matches]"
            return output
        except FileNotFoundError:
            return "Error: ripgrep (rg) is required but not installed."
        except subprocess.TimeoutExpired:
            return "Error: Search timed out after 30 seconds"
        except Exception as exc:
            return f"Error searching: {exc}"

    @mcp.tool()
    def list_existing_fuzzers():
        """List existing fuzzer source files for this project.

        Returns the local file paths of all known fuzzer sources.
        Use ``read_file`` on any path to see its source code.
        """
        import json as _json

        _PATHS_FILE = (
            Path(__file__).resolve().parent.parent.parent.parent
            / "benchmark" / "oss_fuzz_harness" / "coverage"
            / "data" / "gold_source_paths_v2.json"
        )
        project = ctx.project
        if not _PATHS_FILE.exists():
            return f"Error: gold_source_paths_v2.json not found at {_PATHS_FILE}"

        all_paths = _json.loads(_PATHS_FILE.read_text())
        matches = {}
        for case_id, container_path in all_paths.items():
            if case_id.startswith(f"{project}/"):
                # Map $SRC/filename → oss_fuzz_project_dir/filename
                # Map $SRC/repo/path → project_path/path (strip repo name)
                rel = container_path.replace("$SRC/", "")
                # Check if it's in oss-fuzz project dir
                if ctx.oss_fuzz_project_dir:
                    local = Path(ctx.oss_fuzz_project_dir) / rel
                    if local.exists():
                        matches[case_id] = str(local)
                        continue
                # Check if it's in the project repo (strip first component = repo name)
                parts = rel.split("/", 1)
                if len(parts) > 1:
                    local = Path(ctx.project_path) / parts[1]
                    if local.exists():
                        matches[case_id] = str(local)
                        continue
                # Fallback: try project_path directly
                local = Path(ctx.project_path) / rel
                if local.exists():
                    matches[case_id] = str(local)
                    continue
                matches[case_id] = f"(not found locally: {container_path})"

        if not matches:
            return f"No existing fuzzers found for project '{project}'."
        lines = [f"Existing fuzzers for {project}:"]
        for case_id, path in sorted(matches.items()):
            fuzzer_name = case_id.split("/", 1)[1]
            lines.append(f"  {fuzzer_name}: {path}")
        return "\n".join(lines)
