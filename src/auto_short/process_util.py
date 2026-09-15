from __future__ import annotations

import subprocess
from typing import Sequence


def run_cmd(cmd: Sequence[str]) -> subprocess.CompletedProcess[str]:
    """Run a subprocess with UTF-8 stdout/stderr (Windows-safe)."""
    return subprocess.run(
        list(cmd),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def check_output_cmd(cmd: Sequence[str]) -> str:
    return subprocess.check_output(
        list(cmd),
        text=True,
        encoding="utf-8",
        errors="replace",
    )
