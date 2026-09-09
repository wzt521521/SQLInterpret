"""Smoke tests for the shared project skeleton."""

from minidbms.common.errors import DBError, ErrorStage
from minidbms.common.plans import CreateTablePlan
from minidbms.common.types import ColumnDef, DataType, ExecutionResult, SourceLocation
from minidbms.storage.page import PAGE_SIZE


def test_page_size_contract() -> None:
    assert PAGE_SIZE == 4096


def test_create_table_plan_contract() -> None:
    columns = (ColumnDef("id", DataType.INT), ColumnDef("name", DataType.VARCHAR))
    plan = CreateTablePlan("student", columns)
    assert plan.table_name == "student"
    assert plan.columns == columns


def test_error_format_contains_stage_code_and_location() -> None:
    error = DBError(
        ErrorStage.SYNTAX,
        "UNEXPECTED_TOKEN",
        "expected ';'",
        SourceLocation(2, 8),
    )
    rendered = str(error)
    assert "SYNTAX" in rendered
    assert "UNEXPECTED_TOKEN" in rendered
    assert "line 2, column 8" in rendered


def test_execution_result_uses_independent_lists() -> None:
    first = ExecutionResult()
    second = ExecutionResult()
    first.rows.append([1])
    assert second.rows == []
