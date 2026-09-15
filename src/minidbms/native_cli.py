"""Compatibility launcher for the C++ MiniDBMS command-line executable."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


def _find_cli() -> Path:
    configured = os.environ.get("MINIDB_CPP_CLI")
    if configured:
        candidate = Path(configured).expanduser().resolve()
        if candidate.is_file():
            return candidate
        raise RuntimeError(f"MINIDB_CPP_CLI points to a missing file: {candidate}")

    project_root = Path(__file__).resolve().parents[2]
    for directory in (project_root / "build-cpp", project_root / "build"):
        for name in ("minidb_cli.exe", "minidb_cli"):
            candidate = directory / name
            if candidate.is_file():
                return candidate
    discovered = shutil.which("minidb_cli")
    if discovered:
        return Path(discovered).resolve()
    raise RuntimeError(
        "C++ MiniDBMS CLI was not found; run 'cmake -S . -B build-cpp' and "
        "'cmake --build build-cpp' first"
    )


def main(argv: list[str] | None = None) -> int:
    try:
        executable = _find_cli()
        completed = subprocess.run([str(executable), *(sys.argv[1:] if argv is None else argv)])
        return completed.returncode
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
