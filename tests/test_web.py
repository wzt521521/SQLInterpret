"""Browser UI and HTTP API acceptance tests."""

from __future__ import annotations

import json
from pathlib import Path
from threading import Thread
from urllib.request import Request, urlopen

import pytest

from minidbms.web import MiniDBWebApplication, create_server


SQL_EXAMPLES = Path(__file__).parents[1] / "src" / "minidbms" / "web" / "sql_examples"


def get_json(url: str) -> dict:
    with urlopen(url, timeout=5) as response:
        assert response.status == 200
        return json.loads(response.read().decode("utf-8"))


def post_json(url: str, document: dict) -> dict:
    request = Request(
        url,
        data=json.dumps(document).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urlopen(request, timeout=10) as response:
        assert response.status == 200
        return json.loads(response.read().decode("utf-8"))


def post_sql(url: str, sql: str) -> dict:
    return post_json(url, {"sql": sql})


def test_web_frontend_executes_real_pipeline_and_survives_error(tmp_path) -> None:
    db_path = tmp_path / "browser.db"
    application = MiniDBWebApplication(db_path, buffer_capacity=2, replacement_policy="FIFO")
    server = create_server(application, port=0)
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f"http://127.0.0.1:{server.server_port}"
    demo_sql = (
        "CREATE TABLE student(id INT, name VARCHAR, age INT);"
        "INSERT INTO student VALUES(1,'Alice',20);"
        "INSERT INTO student VALUES(2,'Bob',17);"
        "SELECT id,name FROM student WHERE age >= 18;"
        "DELETE FROM student WHERE id = 1;"
        "SELECT * FROM student;"
    )
    try:
        with urlopen(base + "/", timeout=5) as response:
            page = response.read().decode("utf-8")
            assert response.status == 200
            assert "MiniDBMS Studio" in page
            assert "SQL 编辑器" in page
            assert "清空数据库" in page
        with urlopen(base + "/assets/app.js", timeout=5) as response:
            script = response.read().decode("utf-8")
            assert "executeSql" in script
            assert "/api/database/reset" in script

        status = get_json(base + "/api/status")
        assert status["ok"] and status["replacement_policy"] == "FIFO"
        assert status["tables"] == []
        examples = get_json(base + "/api/examples")
        assert len(examples["files"]) == 10
        assert examples["files"][0]["name"] == "01_create_table.sql"
        assert "CREATE TABLE" in examples["files"][0]["sql"]

        executed = post_sql(
            base + "/api/execute",
            demo_sql,
        )
        assert executed["ok"]
        assert len(executed["statements"]) == 6
        assert executed["statements"][3]["result"]["rows"] == [[1, "Alice"]]
        assert executed["statements"][4]["result"]["affected_rows"] == 1
        assert executed["statements"][5]["result"]["rows"] == [[2, "Bob", 17]]
        assert "Project[" in executed["statements"][3]["compiler"]["plan_after"]
        assert executed["status"]["tables"][0]["name"] == "student"

        failed = post_sql(base + "/api/execute", "SELECT missing FROM student;")
        assert not failed["ok"]
        assert failed["error"]["stage"] == "SEMANTIC"
        assert failed["error"]["code"] == "COLUMN_NOT_FOUND"

        recovered = post_sql(base + "/api/execute", "SELECT * FROM student;")
        assert recovered["ok"]
        assert recovered["statements"][0]["result"]["rows"] == [[2, "Bob", 17]]

        reset = post_json(
            base + "/api/database/reset",
            {"confirmation": "RESET_DATABASE"},
        )
        assert reset["ok"]
        assert reset["removed_tables"] == 1
        assert reset["status"]["tables"] == []
        assert Path(reset["backup"]).is_file()

        repeated = post_sql(base + "/api/execute", demo_sql)
        assert repeated["ok"], repeated["error"]
        assert repeated["statements"][-1]["result"]["rows"] == [[2, "Bob", 17]]
    finally:
        server.shutdown()
        thread.join(timeout=5)
        server.server_close()
        application.close()

    reopened = MiniDBWebApplication(db_path)
    try:
        payload = reopened.execute("SELECT * FROM student;")
        assert payload["ok"]
        assert payload["statements"][0]["result"]["rows"] == [[2, "Bob", 17]]
    finally:
        reopened.close()


def test_reset_empty_database_is_idempotent(tmp_path) -> None:
    application = MiniDBWebApplication(tmp_path / "empty.db")
    try:
        payload = application.reset()
        assert payload["ok"]
        assert payload["removed_tables"] == 0
        assert payload["backup"] is None
        assert payload["status"]["tables"] == []
    finally:
        application.close()


@pytest.mark.parametrize("filename", [
    "01_create_table.sql",
    "02_insert_and_select.sql",
    "03_where_comparisons.sql",
    "04_boolean_and_arithmetic.sql",
    "05_projection_and_delete.sql",
    "06_comments_strings_and_case.sql",
    "07_complete_feature_demo.sql",
])
def test_success_sql_example_runs_from_frontend(filename, tmp_path) -> None:
    application = MiniDBWebApplication(tmp_path / f"{filename}.db")
    try:
        payload = application.execute((SQL_EXAMPLES / filename).read_text(encoding="utf-8"))
        assert payload["ok"], payload["error"]
        assert payload["statements"]
        assert {item["category"] for item in payload["statements"]} <= {
            "schema", "mutation", "query"
        }
    finally:
        application.close()


@pytest.mark.parametrize("filename,stage,code", [
    ("08_lexical_error.sql", "LEXICAL", "ILLEGAL_CHARACTER"),
    ("09_syntax_error.sql", "SYNTAX", "UNEXPECTED_TOKEN"),
    ("10_semantic_error.sql", "SEMANTIC", "COLUMN_NOT_FOUND"),
])
def test_error_sql_example_reports_expected_stage(filename, stage, code, tmp_path) -> None:
    application = MiniDBWebApplication(tmp_path / f"{filename}.db")
    try:
        payload = application.execute((SQL_EXAMPLES / filename).read_text(encoding="utf-8"))
        assert not payload["ok"]
        assert payload["error"]["stage"] == stage
        assert payload["error"]["code"] == code
    finally:
        application.close()
