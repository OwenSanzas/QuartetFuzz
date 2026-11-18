from __future__ import annotations

import subprocess
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path
from threading import Thread
from typing import TextIO

from rich.console import Console
from rich.markup import escape


@dataclass(frozen=True)
class StreamedCommandResult:
    returncode: int
    stdout: str
    stderr: str
    timed_out: bool = False

    @property
    def combined_output(self) -> str:
        if self.stdout and self.stderr:
            return self.stdout + "\n" + self.stderr
        return self.stdout or self.stderr


def _pump_stream(
    stream: TextIO | Iterable[str] | None,
    *,
    sink: list[str],
    stream_to: Console | None,
    stream_prefix: str,
) -> None:
    if stream is None:
        return

    try:
        for raw_line in stream:
            sink.append(raw_line)
            line = raw_line.rstrip()
            if stream_to and line:
                stream_to.print(f"         [dim]{escape(stream_prefix + line)}[/dim]")
    finally:
        close = getattr(stream, "close", None)
        if callable(close):
            close()


def run_command_with_streaming(
    cmd: list[str],
    *,
    cwd: Path,
    timeout: int | None = None,
    env: dict[str, str] | None = None,
    stream_to: Console | None = None,
    stream_prefix: str = "",
) -> StreamedCommandResult:
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
        cwd=str(cwd),
        env=env,
    )

    stdout_lines: list[str] = []
    stderr_lines: list[str] = []
    stdout_thread = Thread(
        target=_pump_stream,
        kwargs={
            "stream": proc.stdout,
            "sink": stdout_lines,
            "stream_to": stream_to,
            "stream_prefix": stream_prefix,
        },
        daemon=True,
    )
    stderr_thread = Thread(
        target=_pump_stream,
        kwargs={
            "stream": proc.stderr,
            "sink": stderr_lines,
            "stream_to": stream_to,
            "stream_prefix": stream_prefix,
        },
        daemon=True,
    )
    stdout_thread.start()
    stderr_thread.start()

    timed_out = False
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        proc.kill()
        proc.wait()

    stdout_thread.join()
    stderr_thread.join()

    return StreamedCommandResult(
        returncode=proc.returncode if proc.returncode is not None else -1,
        stdout="".join(stdout_lines),
        stderr="".join(stderr_lines),
        timed_out=timed_out,
    )
