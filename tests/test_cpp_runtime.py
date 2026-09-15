"""Black-box acceptance tests for the C++ database runtime."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


PROJECT_ROOT = Path(__file__).parents[1]


def executable(name: str) -> Path:
    configured = os.environ.get("MINIDB_CPP_BUILD")
    build = Path(configured).resolve() if configured else PROJECT_ROOT / "build-cpp"
    suffix = ".exe" if os.name == "nt" else ""
    result = build / f"{name}{suffix}"
    assert result.is_file(), f"missing {result}; build the C++ project first"
    return result


def test_cpp_cli_executes_file_and_survives_restart(tmp_path: Path) -> None:
    db_path = tmp_path / "cli.db"
    command = [str(executable("minidb_cli")), "--db", str(db_path)]
    first = subprocess.run(
        [*command, "--file", str(PROJECT_ROOT / "tests" / "e2e_demo.sql"), "--stats"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=30,
    )
    assert first.returncode == 0, first.stderr
    assert "1 | Alice" in first.stdout and "3 | Carol" in first.stdout
    assert "1 row(s) deleted" in first.stdout
    assert "cache_hits" in first.stdout

    second = subprocess.run(
        command,
        input="SELECT * FROM student;\nexit\n",
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=30,
    )
    assert second.returncode == 0, second.stderr
    assert "2 | Bob | 17" in second.stdout
    assert "3 | Carol | 22" in second.stdout
    assert "1 | Alice" not in second.stdout


def test_cpp_bridge_returns_web_contract_and_errors(tmp_path: Path) -> None:
    db_path = tmp_path / "bridge.db"
    command = [
        str(executable("minidb_core_bridge")),
        "execute",
        "--db", str(db_path),
        "--buffer-capacity", "2",
        "--replacement-policy", "FIFO",
    ]
    completed = subprocess.run(
        command,
        input=(
            "CREATE TABLE t(id INT, name VARCHAR);"
            "INSERT INTO t VALUES(1,'C++');"
            "SELECT * FROM t;"
        ).encode("utf-8"),
        capture_output=True,
        timeout=30,
    )
    assert completed.returncode == 0, completed.stderr.decode("utf-8", errors="replace")
    payload = json.loads(completed.stdout.decode("utf-8"))
    assert payload["ok"]
    assert payload["statements"][-1]["result"]["rows"] == [[1, "C++"]]
    assert "SeqScan" in payload["statements"][-1]["compiler"]["plan_after"]

    failed = subprocess.run(
        command,
        input=b"SELECT missing FROM t;",
        capture_output=True,
        timeout=30,
    )
    error = json.loads(failed.stdout.decode("utf-8"))
    assert failed.returncode == 0
    assert not error["ok"]
    assert error["error"]["stage"] == "SEMANTIC"
    assert error["error"]["code"] == "COLUMN_NOT_FOUND"


def test_cpp_bridge_reset_allows_repeating_the_same_demo(tmp_path: Path) -> None:
    db_path = tmp_path / "reset.db"
    bridge = executable("minidb_core_bridge")
    common = [
        "--db", str(db_path),
        "--buffer-capacity", "2",
        "--replacement-policy", "LRU",
    ]
    sql = "CREATE TABLE demo(id INT); INSERT INTO demo VALUES(1); SELECT * FROM demo;"

    def invoke(operation: str, input_sql: str = "") -> dict:
        completed = subprocess.run(
            [str(bridge), operation, *common],
            input=input_sql.encode("utf-8"),
            capture_output=True,
            timeout=30,
        )
        assert completed.returncode == 0, completed.stderr.decode("utf-8", errors="replace")
        return json.loads(completed.stdout.decode("utf-8"))

    assert invoke("execute", sql)["ok"]
    reset = invoke("reset")
    assert reset["ok"]
    assert reset["removed_tables"] == 1
    assert reset["status"]["tables"] == []
    assert Path(reset["backup"]).is_file()

    repeated = invoke("execute", sql)
    assert repeated["ok"], repeated["error"]
    assert repeated["statements"][-1]["result"]["rows"] == [[1]]
