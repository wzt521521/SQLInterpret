"""Reproducible full-system demo with a separate process for restart verification."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

from minidbms.engine import Database


PROJECT = Path(__file__).resolve().parents[1]
DEMO_SQL = PROJECT / "tests" / "e2e_demo.sql"


def run(command: list[str], env: dict[str, str]) -> str:
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", env=env)
    if completed.returncode:
        raise RuntimeError(f"command failed ({completed.returncode}):\n{completed.stderr}")
    return completed.stdout


def main() -> None:
    env = os.environ.copy()
    env["PYTHONPATH"] = str(PROJECT / "src") + os.pathsep + env.get("PYTHONPATH", "")
    with TemporaryDirectory(prefix="minidb-e2e-") as directory:
        db_path = Path(directory) / "demo.db"
        first = [sys.executable, "-m", "minidbms", "--db", str(db_path),
                 "--buffer-capacity", "2", "--file", str(DEMO_SQL), "--stats"]
        print("=== First process: CREATE / INSERT / SELECT / DELETE ===")
        print(run(first, env).strip())

        verification = Path(directory) / "verify.sql"
        verification.write_text("SELECT * FROM student;\n", encoding="utf-8")
        second = [sys.executable, "-m", "minidbms", "--db", str(db_path),
                  "--file", str(verification)]
        print("\n=== Second process: restart verification ===")
        print(run(second, env).strip())

        with Database(db_path) as db:
            detail = db.compiler.compile_detailed(
                "SELECT id, name FROM student WHERE TRUE AND age >= 10+8;", db.catalog
            )[0]
            print("\n=== SQL compiler evidence ===")
            print("tokens:", [token["lexeme"] for token in detail.tokens])
            print("AST:", type(detail.ast).__name__)
            print("semantic:", detail.native["semantic"])
            print("plan before:", detail.plan_before)
            print("plan after:", detail.plan_after)


if __name__ == "__main__":
    main()
