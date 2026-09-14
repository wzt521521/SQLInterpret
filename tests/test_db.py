"""Persistent engine, shared-plan and CLI acceptance tests."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

import pytest

from minidbms.common.errors import DBError, ErrorStage
from minidbms.common.expressions import BinaryExpr, IdentifierExpr, LiteralExpr, UnaryExpr
from minidbms.common.plans import CreateTablePlan, FilterPlan, ProjectPlan, SeqScanPlan
from minidbms.common.types import ColumnDef, DataType, TableSchema
from minidbms.engine import Database
from minidbms.engine.catalog_manager import CatalogManager
from minidbms.engine.errors import ExecutionError
from minidbms.engine.expression_eval import ExpressionEvaluator
from minidbms.engine.record import RecordCodec, RowPage
from minidbms.storage import StorageManager


def error_code(code: str, operation) -> None:
    with pytest.raises(DBError) as caught:
        operation()
    assert caught.value.code == code


def student_schema() -> TableSchema:
    return TableSchema("student", (
        ColumnDef("id", DataType.INT), ColumnDef("name", DataType.VARCHAR),
        ColumnDef("age", DataType.INT),
    ))


def test_record_codec_and_slotted_page_boundaries() -> None:
    codec = RecordCodec()
    schema = student_schema()
    values = [-(1 << 63), "a\n;\x00中'文", (1 << 63) - 1]
    encoded = codec.encode(schema, values)
    assert codec.decode(schema, encoded) == values
    assert codec.decode(schema, codec.encode(schema, [0, "", 0])) == [0, "", 0]
    error_code("TYPE_MISMATCH", lambda: codec.encode(schema, [True, "x", 1]))
    error_code("TYPE_MISMATCH", lambda: codec.encode(schema, [1 << 63, "x", 1]))
    error_code("VALUE_COUNT_MISMATCH", lambda: codec.encode(schema, [1, "x"]))
    error_code("CORRUPT_RECORD", lambda: codec.decode(schema, encoded[:-1]))
    error_code("CORRUPT_RECORD", lambda: codec.decode(schema, encoded + b"x"))

    page = RowPage.empty()
    first = page.insert(encoded)
    second = page.insert(codec.encode(schema, [2, "Bob", 20]))
    assert [slot for slot, _ in page.iter_live()] == [first, second]
    assert page.delete(first)
    assert not page.delete(first)
    reopened = RowPage(page.to_bytes())
    assert [slot for slot, _ in reopened.iter_live()] == [second]
    error_code("CORRUPT_PAGE", lambda: RowPage(bytes(4096)))
    error_code("ROW_NOT_FOUND", lambda: reopened.delete(99))


def test_catalog_is_the_compiler_view_and_survives_restart(tmp_path: Path) -> None:
    path = tmp_path / "catalog.db"
    with StorageManager(path) as storage:
        catalog = CatalogManager(storage)
        assert not catalog.table_exists("STUDENT")
        catalog.create_table(student_schema())
        assert catalog.table_exists("STUDENT")
        assert catalog.get_schema("student") == student_schema()
        error_code("TABLE_EXISTS", lambda: catalog.create_table(student_schema()))
        error_code("TABLE_NOT_FOUND", lambda: catalog.get_schema("missing"))
        data_page = storage.allocate_page()
        catalog.add_page("student", data_page)
        assert catalog.page_ids("student") == (data_page,)
    with StorageManager(path) as storage:
        catalog = CatalogManager(storage)
        assert catalog.get_schema("student") == student_schema()
        assert catalog.page_ids("student") == (data_page,)


def test_catalog_grows_beyond_one_page_and_detects_corruption(tmp_path: Path) -> None:
    path = tmp_path / "many-tables.db"
    with StorageManager(path, buffer_capacity=2) as storage:
        catalog = CatalogManager(storage)
        for index in range(90):
            catalog.create_table(TableSchema(f"table_{index}", (
                ColumnDef("id", DataType.INT), ColumnDef("description", DataType.VARCHAR),
            )))
        assert catalog.table_exists("table_89")
    with StorageManager(path, buffer_capacity=2) as storage:
        catalog = CatalogManager(storage)
        assert catalog.get_schema("table_89").columns[1].name == "description"
        storage.write_page(0, b"invalid catalog root")
    with StorageManager(path) as storage:
        error_code("CORRUPT_CATALOG", lambda: CatalogManager(storage))


def test_catalog_failed_commit_does_not_publish_table(tmp_path: Path, monkeypatch) -> None:
    with StorageManager(tmp_path / "rollback.db") as storage:
        catalog = CatalogManager(storage)
        original_flush = storage.flush_page
        failed = False

        def fail_once(page_id: int) -> None:
            nonlocal failed
            if page_id == 0 and not failed:
                failed = True
                raise ExecutionError("INJECTED_FAILURE", "root flush failed")
            original_flush(page_id)

        monkeypatch.setattr(storage, "flush_page", fail_once)
        error_code("INJECTED_FAILURE", lambda: catalog.create_table(student_schema()))
        assert not catalog.table_exists("student")
        monkeypatch.setattr(storage, "flush_page", original_flush)
    with StorageManager(tmp_path / "rollback.db") as storage:
        assert not CatalogManager(storage).table_exists("student")


def test_plan_execution_and_expression_rules(tmp_path: Path) -> None:
    with Database(tmp_path / "plans.db") as db:
        schema = student_schema()
        db.executor.execute(CreateTablePlan("student", schema.columns))
        db.execute("INSERT INTO student(name,age,id) VALUES('Alice',20,1);")
        db.execute("INSERT INTO student VALUES(2,'Bob',17);")
        expr = BinaryExpr(IdentifierExpr("age"), ">", LiteralExpr(18, DataType.INT))
        plan = ProjectPlan(("name", "id"), FilterPlan(expr, SeqScanPlan("student")))
        result = db.executor.execute(plan)
        assert result.columns == ["name", "id"]
        assert result.rows == [["Alice", 1]]
        assert db.executor.execute(SeqScanPlan("student")).rows == [[1, "Alice", 20], [2, "Bob", 17]]

    evaluator = ExpressionEvaluator()
    bad = IdentifierExpr("missing")
    assert evaluator.evaluate(BinaryExpr(LiteralExpr(False, DataType.BOOL), "AND", bad), {}) is False
    assert evaluator.evaluate(BinaryExpr(LiteralExpr(True, DataType.BOOL), "OR", bad), {}) is True
    assert evaluator.evaluate(UnaryExpr("NOT", LiteralExpr(False, DataType.BOOL)), {}) is True
    assert evaluator.evaluate(BinaryExpr(LiteralExpr("中", DataType.VARCHAR), ">", LiteralExpr("A", DataType.VARCHAR)), {}) is True
    error_code("COLUMN_NOT_FOUND", lambda: evaluator.evaluate(bad, {}))
    error_code("INTEGER_OVERFLOW", lambda: evaluator.evaluate(
        BinaryExpr(LiteralExpr((1 << 63) - 1, DataType.INT), "+", LiteralExpr(1, DataType.INT)), {}
    ))


def test_e2e_sql_delete_and_restart(tmp_path: Path) -> None:
    db_path = tmp_path / "e2e.db"
    script = (Path(__file__).parent / "e2e_demo.sql").read_text(encoding="utf-8")
    with Database(db_path, buffer_capacity=2, replacement_policy="FIFO") as db:
        results = db.execute(script)
        assert len(results) == 7
        assert results[4].columns == ["id", "name"]
        assert results[4].rows == [[1, "Alice"], [3, "Carol"]]
        assert results[5].affected_rows == 1
        assert results[6].rows == [[2, "Bob", 17], [3, "Carol", 22]]
        assert db.stats()["cache_misses"] > 0
    with Database(db_path, buffer_capacity=2) as reopened:
        result = reopened.execute("SELECT * FROM student;")[0]
        assert result.rows == [[2, "Bob", 17], [3, "Carol", 22]]
        assert reopened.catalog.get_schema("student") == student_schema()


def test_cross_page_scan_delete_and_restart(tmp_path: Path) -> None:
    db_path = tmp_path / "cross-page.db"
    with Database(db_path, buffer_capacity=2) as db:
        db.execute("CREATE TABLE items(id INT, name VARCHAR);")
        for index in range(24):
            db.execute(f"INSERT INTO items VALUES({index}, '{'x' * 700}');")
        assert len(db.catalog.page_ids("items")) >= 4
        assert [row[0] for row in db.execute("SELECT id FROM items;")[0].rows] == list(range(24))
        assert db.execute("DELETE FROM items WHERE id >= 12 AND id < 18;")[0].affected_rows == 6
        assert db.stats()["evictions"] > 0
    with Database(db_path, buffer_capacity=2) as db:
        ids = [row[0] for row in db.execute("SELECT id FROM items;")[0].rows]
        assert ids == list(range(12)) + list(range(18, 24))
        assert len(db.catalog.page_ids("items")) >= 4


def test_delete_without_where_and_corrupt_data_page(tmp_path: Path) -> None:
    with Database(tmp_path / "delete-all.db") as db:
        db.execute("CREATE TABLE t(id INT); INSERT INTO t VALUES(1); INSERT INTO t VALUES(2);")
        assert db.execute("DELETE FROM t;")[0].affected_rows == 2
        assert db.execute("SELECT * FROM t;")[0].rows == []
        page_id = db.catalog.page_ids("t")[0]
        db.storage.write_page(page_id, b"invalid row page")
        error_code("CORRUPT_PAGE", lambda: db.execute("SELECT * FROM t;"))


def test_failed_insert_leaves_no_partial_row_or_unregistered_page(tmp_path: Path, monkeypatch) -> None:
    with Database(tmp_path / "failed-insert.db") as db:
        db.execute("CREATE TABLE t(id INT, name VARCHAR);")
        error_code("RECORD_TOO_LARGE", lambda: db.rows.insert("t", [1, "x" * 4096]))
        assert db.catalog.page_ids("t") == ()
        original = db.catalog.add_page

        def fail(_name: str, _page: int) -> None:
            raise ExecutionError("INJECTED_FAILURE", "catalog update failed")

        monkeypatch.setattr(db.catalog, "add_page", fail)
        error_code("INJECTED_FAILURE", lambda: db.execute("INSERT INTO t VALUES(1,'x');"))
        monkeypatch.setattr(db.catalog, "add_page", original)
        assert db.catalog.page_ids("t") == ()
        assert db.execute("SELECT * FROM t;")[0].rows == []
        assert db.execute("INSERT INTO t VALUES(2,'ok');")[0].affected_rows == 1


@pytest.mark.parametrize("sql,stage,code", [
    ("CREATE TABLE t(id INT); CREATE TABLE t(id INT);", ErrorStage.SEMANTIC, "TABLE_ALREADY_EXISTS"),
    ("SELECT * FROM missing;", ErrorStage.SEMANTIC, "TABLE_NOT_FOUND"),
    ("CREATE TABLE t(id INT); INSERT INTO t VALUES('bad');", ErrorStage.SEMANTIC, "TYPE_MISMATCH"),
    ("CREATE TABLE t(id INT); INSERT INTO t VALUES(1,2);", ErrorStage.SEMANTIC, "VALUE_COUNT_MISMATCH"),
    ("SELECT * FROM t", ErrorStage.SYNTAX, "UNEXPECTED_TOKEN"),
    ("SELECT @;", ErrorStage.LEXICAL, "ILLEGAL_CHARACTER"),
])
def test_compiler_errors_preserve_stage(tmp_path: Path, sql: str, stage: ErrorStage, code: str) -> None:
    with Database(tmp_path / "errors.db") as db:
        with pytest.raises(DBError) as caught:
            db.execute(sql)
        assert (caught.value.stage, caught.value.code) == (stage, code)


def cli_env() -> dict[str, str]:
    env = os.environ.copy()
    src = str(Path(__file__).resolve().parents[1] / "src")
    env["PYTHONPATH"] = src + os.pathsep + env.get("PYTHONPATH", "")
    return env


def test_cli_file_and_cross_process_restart(tmp_path: Path) -> None:
    db_path = tmp_path / "cli.db"
    command = [sys.executable, "-m", "minidbms", "--db", str(db_path), "--buffer-capacity", "2"]
    first = subprocess.run(command + ["--file", str(Path(__file__).parent / "e2e_demo.sql"), "--stats"],
                           capture_output=True, text=True, encoding="utf-8", env=cli_env())
    assert first.returncode == 0, first.stderr
    assert "1 | Alice" in first.stdout and "3 | Carol" in first.stdout
    assert "1 row(s) deleted" in first.stdout
    second = subprocess.run(command, input="SELECT * FROM student;\nexit\n", capture_output=True,
                            text=True, encoding="utf-8", env=cli_env())
    assert second.returncode == 0, second.stderr
    assert "2 | Bob | 17" in second.stdout
    assert "3 | Carol | 22" in second.stdout
    assert "1 | Alice" not in second.stdout


def test_cli_survives_error_and_quoted_semicolon(tmp_path: Path) -> None:
    command = [sys.executable, "-m", "minidbms", "--db", str(tmp_path / "interactive.db")]
    sql = ("CREATE TABLE t(id INT, name VARCHAR);\n"
           "INSERT INTO t VALUES(1,'a;''b');\n"
           "SELECT missing FROM t;\n"
           "/* comment; */ SELECT name FROM t;\n"
           "stats\nquit\n")
    result = subprocess.run(command, input=sql, capture_output=True, text=True,
                            encoding="utf-8", env=cli_env())
    assert result.returncode == 0
    assert "SEMANTIC:COLUMN_NOT_FOUND" in result.stderr
    assert "a;'b" in result.stdout
    assert "cache_hits" in result.stdout


def test_file_cli_reports_error_but_executes_later_statements(tmp_path: Path) -> None:
    script = tmp_path / "mixed.sql"
    script.write_text(
        "CREATE TABLE t(id INT); SELECT missing FROM t; INSERT INTO t VALUES(7); SELECT * FROM t;",
        encoding="utf-8",
    )
    result = subprocess.run(
        [sys.executable, "-m", "minidbms", "--db", str(tmp_path / "mixed.db"),
         "--file", str(script)],
        capture_output=True, text=True, encoding="utf-8", env=cli_env(),
    )
    assert result.returncode == 1
    assert "SEMANTIC:COLUMN_NOT_FOUND" in result.stderr
    assert "7" in result.stdout
