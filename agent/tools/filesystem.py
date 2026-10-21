"""The three filesystem tools every AGF agent mounts: ``Read``, ``Glob``, ``Grep``.

They mirror Claude Code's read-only trio. Closures over a
:class:`~agent.tools.workspace.Workspace`, returned as the list
``BaseAgent.get_tools`` expects.

PydanticAI builds each tool's schema from the signature and docstring below, so
edits here change what the model is told, not just what the code does.
"""

from __future__ import annotations

import subprocess
from collections.abc import Callable
from pathlib import Path

from agent.tools.workspace import Workspace

__all__ = ["build_filesystem_tools", "format_numbered_lines"]

# Per-line cap. One minified bundle or embedded blob would otherwise consume
# the whole result.
_MAX_LINE_CHARS = 2000

# Glob results beyond this are replaced by a count.
_MAX_GLOB_RESULTS = 200

_SEARCH_TIMEOUT_S = 30

_OUTPUT_MODES = ("files_with_matches", "content", "count")


def _number_lines(lines: list[str], offset: int, limit: int) -> str:
    """Number and slice already-split *lines*.

    Splitting is the caller's job because the two callers must do it
    differently: a file read uses ``readlines``, in-memory text uses
    ``splitlines(keepends=True)``. They disagree on form feed and U+2028.
    """
    total = len(lines)
    selected = lines[offset : offset + limit]
    out: list[str] = []
    for i, line in enumerate(selected, start=offset + 1):
        out.append("{:>6}\t{}".format(i, line.rstrip("\n")[:_MAX_LINE_CHARS]))
    header = f"[{total} lines total, showing {offset + 1}-{offset + len(selected)}]"
    return header + "\n" + "\n".join(out)


def format_numbered_lines(text: str, *, offset: int = 0, limit: int = 2000) -> str:
    """Render in-memory *text* as the numbered slice ``Read`` returns.

    Exported so a :data:`~agent.tools.workspace.ReadHook` can match the shape of
    a real read. ``ap`` uses it for the harness it has not written out yet.
    """
    return _number_lines(text.splitlines(keepends=True), offset, limit)


def _relativize_to_roots(text: str, roots: tuple[Path, ...]) -> str:
    """Strip an allowed-root prefix from each line of ripgrep output.

    ripgrep is given absolute directories, so every hit starts with the absolute
    project path. Longest prefix first, or a nested root is masked by its parent.
    """
    if not text:
        return text
    prefixes = sorted({str(r).rstrip("/") + "/" for r in roots}, key=len, reverse=True)
    out_lines: list[str] = []
    for line in text.split("\n"):
        for prefix in prefixes:
            if line.startswith(prefix):
                line = line[len(prefix) :]
                break
        out_lines.append(line)
    return "\n".join(out_lines)


def _display_path(p: Path, ws: Workspace) -> str:
    if ws.search_path_style == "absolute":
        return str(p)
    for root in sorted(ws.roots, key=lambda r: len(str(r)), reverse=True):
        try:
            return str(p.relative_to(root))
        except ValueError:
            continue
    return str(p)


def build_filesystem_tools(ws: Workspace) -> list[Callable[..., str]]:
    """Build ``Read`` / ``Glob`` / ``Grep`` bound to *ws*."""

    def Read(file_path: str, offset: int = 1, limit: int = 2000) -> str:  # noqa: N802
        """Read a file from the project. Returns content with line numbers.

        Use Grep or Glob first to find the file, then Read it. For a large
        file read it in slices with ``offset`` and ``limit``.

        Args:
            file_path: Path to file (absolute, or relative to project root).
            offset: Line number to start reading from (1-based, as shown in
                the line numbers this tool and Grep return).
            limit: Maximum number of lines to read.
        """
        p, err = ws.resolve(file_path)
        if err:
            return err
        assert p is not None

        # The model counts from 1, like Claude Code's Read and like the line
        # numbers it is shown. Everything behind this point, the read hook
        # included, slices from 0. Values below 1 mean "from the top" rather
        # than Python's count-from-the-end.
        start = max(offset, 1) - 1

        if ws.read_hook is not None:
            supplied = ws.read_hook(p, start, limit)
            if supplied is not None:
                return ws.truncate(supplied)

        if not p.exists():
            return f"Error: File not found: {file_path}"
        if not p.is_file():
            return f"Error: Not a file: {file_path}"

        try:
            with open(p, errors="replace") as f:
                lines = f.readlines()
        except OSError as e:
            # Permission denied, a race with a build, an unreadable device.
            # The model can act on a message; an exception ends its turn.
            return f"Error reading file: {e}"

        return ws.truncate(_number_lines(lines, start, limit))

    def Glob(pattern: str, path: str = "") -> str:  # noqa: N802
        """Find files by name pattern. Returns matching paths, newest first.

        Supports ``**`` for any depth, e.g. ``"**/*.c"``, ``"src/**/*_fuzzer.c"``,
        ``"include/*.h"``. Use ``"*"`` to list one directory. Only files are
        returned, never directories.

        Args:
            pattern: Glob pattern, relative to ``path``.
            path: Directory to search in (absolute, or relative to project
                root). Defaults to the project root.
        """
        d, err = ws.resolve(path or ".", is_dir=True)
        if err:
            return err
        assert d is not None
        if not d.exists():
            return f"Error: Directory not found: {path or '.'}"
        if not d.is_dir():
            return f"Error: Not a directory: {path or '.'}"
        if not pattern or Path(pattern).is_absolute():
            return "Error: pattern must be a relative glob such as '**/*.c'"

        hits: list[tuple[float, Path]] = []
        try:
            for candidate in d.glob(pattern):
                if ".git" in candidate.relative_to(d).parts:
                    continue
                try:
                    if not candidate.is_file():
                        continue
                    mtime = candidate.stat().st_mtime
                except OSError:
                    # Broken symlink, or a race with a build. Skip it rather
                    # than lose the whole listing.
                    continue
                hits.append((mtime, candidate))
        except (ValueError, NotImplementedError) as e:
            return f"Error: Invalid glob pattern {pattern!r}: {e}"

        if not hits:
            return f"No files found for {pattern!r} in {path or '.'}"

        hits.sort(key=lambda t: (-t[0], str(t[1])))
        shown = hits[:_MAX_GLOB_RESULTS]
        out_lines = [_display_path(p, ws) for _, p in shown]
        header = f"[{len(hits)} files]"
        body = "\n".join(out_lines)
        if len(hits) > _MAX_GLOB_RESULTS:
            body += f"\n\n[... {len(hits) - _MAX_GLOB_RESULTS} more files; narrow the pattern or path]"
        return ws.truncate(header + "\n" + body)

    def Grep(  # noqa: N802
        pattern: str,
        path: str = "",
        glob: str = "",
        output_mode: str = "files_with_matches",
        case_insensitive: bool = False,
        context: int = 0,
        head_limit: int = 100,
    ) -> str:
        """Search file contents with a regex (ripgrep syntax).

        Start with the default ``files_with_matches`` to learn which files
        mention a symbol, then switch to ``content`` to see the matching lines
        with line numbers, or ``count`` for per-file hit counts. Anchor the
        pattern at line start to find definitions rather than callers
        (``"^my_func\\\\("``). Narrow with ``path`` or ``glob`` when there are
        too many hits.

        Args:
            pattern: Regex pattern.
            path: File or directory to search (absolute, or relative to
                project root). Defaults to the project root.
            glob: Filter files by glob, e.g. ``"*.h"``. Empty falls back to
                the language default for this run.
            output_mode: ``"files_with_matches"`` (default), ``"content"``, or
                ``"count"``.
            case_insensitive: Ignore case.
            context: Lines of context around each match. Only used with
                ``output_mode="content"``.
            head_limit: Maximum number of output lines before the rest is
                summarised as a count.
        """
        if output_mode not in _OUTPUT_MODES:
            return f"Error: output_mode must be one of {', '.join(_OUTPUT_MODES)}; got {output_mode!r}"
        target, err = ws.resolve(path or ".", is_dir=True)
        if err:
            return err
        assert target is not None

        effective_glob = glob or ws.default_glob
        mode_flag = {"files_with_matches": "-l", "content": "-n", "count": "-c"}[output_mode]
        # --hidden because a source tree keeps real content in dotfiles
        # (.clang-format, .github/, .cargo/config.toml) and ripgrep skips them
        # by default. That would also open .git, so it is excluded by name.
        # --no-ignore because ripgrep otherwise honours .gitignore, and
        # projects ignore real source there (capstone lists suite/fuzz/, so
        # every fuzzer in the tree was invisible). Glob never applied it.
        cmd = ["rg", mode_flag, "--hidden", "--no-ignore", "--glob", "!.git"]
        if case_insensitive:
            cmd.append("-i")
        if output_mode == "content" and context > 0:
            cmd += ["-C", str(context)]
        if effective_glob:
            cmd += ["--glob", effective_glob]
        cmd += [pattern, str(target)]

        engine = "ripgrep"
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=_SEARCH_TIMEOUT_S)
        except FileNotFoundError:
            if ws.missing_ripgrep == "error":
                return "Error: ripgrep (rg) is required but not installed."
            engine = "grep"
            fallback = ["grep", "-r", "--exclude-dir=.git", mode_flag]
            if case_insensitive:
                fallback.append("-i")
            if output_mode == "content" and context > 0:
                fallback += ["-C", str(context)]
            if effective_glob:
                fallback.append(f"--include={effective_glob}")
            fallback += [pattern, str(target)]
            try:
                result = subprocess.run(fallback, capture_output=True, text=True, timeout=_SEARCH_TIMEOUT_S)
            except subprocess.TimeoutExpired:
                return f"Error: Search timed out after {_SEARCH_TIMEOUT_S} seconds"
            except Exception as e:
                return f"Error searching: {e}"
        except subprocess.TimeoutExpired:
            return f"Error: Search timed out after {_SEARCH_TIMEOUT_S} seconds"
        except Exception as e:
            return f"Error searching: {e}"

        # A model looking for a call naturally searches ``func(``, which is an
        # unbalanced group and not a valid regex. Rather than fail, search it
        # once more as a literal string, which is what the caller meant.
        # grep's POSIX regex accepts these patterns, so only ripgrep needs it.
        literal = False
        if engine == "ripgrep" and result.returncode == 2 and "regex parse error" in result.stderr.lower():
            try:
                retry = subprocess.run(
                    [cmd[0], "--fixed-strings", *cmd[1:]],
                    capture_output=True,
                    text=True,
                    timeout=_SEARCH_TIMEOUT_S,
                )
            except subprocess.TimeoutExpired:
                return f"Error: Search timed out after {_SEARCH_TIMEOUT_S} seconds"
            except Exception as e:
                return f"Error searching: {e}"
            if retry.returncode in (0, 1):
                result, literal = retry, True

        # Exit 0 = matches, 1 = no matches. Anything else is a real failure:
        # invalid regex, bad glob, permission. Reporting those as "no matches"
        # would tell the model the symbol does not exist. Both engines use the
        # same two codes.
        if result.returncode not in (0, 1):
            stderr = result.stderr.strip()[:500]
            return f"Error: {engine} failed (rc={result.returncode}): {stderr or '(no stderr)'}"

        output = result.stdout.strip()
        if output_mode == "count":
            # rg -c lists every searched file including zero counts only with
            # --count-matches semantics; drop the ":0" rows so the result reads
            # as "files that matched, with counts".
            output = "\n".join(line for line in output.split("\n") if line and not line.endswith(":0"))
        if not output:
            how = " as a literal string, since it is not a valid regex" if literal else ""
            return f"No matches found for {pattern!r}{how} in {path or '.'} (glob: {effective_glob or 'none'})"
        if ws.search_path_style == "relative":
            output = _relativize_to_roots(output, ws.roots)
        if literal:
            output = f"[pattern is not a valid regex; searched for it as a literal string]\n{output}"
        if engine == "grep":
            # POSIX BRE, not Rust regex. Say so, or the model reads another
            # dialect's result as if it were ripgrep's.
            output = f"[ripgrep not installed; searched with grep, which uses POSIX regex]\n{output}"
        lines = output.split("\n")
        if head_limit > 0 and len(lines) > head_limit:
            head = "\n".join(lines[:head_limit])
            return ws.truncate(head + f"\n\n[... {len(lines) - head_limit} more matches]")
        return ws.truncate(output)

    if ws.glob_in_signature:
        # Patching __defaults__ keeps one definition of the function while
        # letting the schema show this workspace's actual glob. ``glob`` is the
        # second defaulted parameter.
        defaults = list(Grep.__defaults__ or ())
        defaults[1] = ws.default_glob
        Grep.__defaults__ = tuple(defaults)

    return [Read, Glob, Grep]
