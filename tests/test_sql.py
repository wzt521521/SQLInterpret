"""Python/C++ contract acceptance. Build native binaries before running pytest."""
from dataclasses import asdict
import json

import pytest

from minidbms.common.errors import DBError, ErrorStage
from minidbms.common.expressions import IdentifierExpr, BinaryExpr, UnaryExpr, LiteralExpr
from minidbms.common.plans import (CreateTablePlan, InsertPlan, SeqScanPlan,
                                  FilterPlan, ProjectPlan, DeletePlan)
from minidbms.common.types import ColumnDef, DataType, TableSchema, ExecutionResult
from minidbms.sql_compiler import SQLCompiler
from minidbms.sql_compiler.lexer import Lexer
from minidbms.sql_compiler.parser import Parser
from minidbms.sql_compiler.semantic import SemanticAnalyzer
from minidbms.sql_compiler.planner import PlanGenerator
from minidbms.sql_compiler.optimizer import PlanOptimizer
from minidbms.sql_compiler._convert import evaluate_expression


class Catalog:
    def __init__(self):
        self.schemas = {"t": TableSchema("t", (
            ColumnDef("a", DataType.INT), ColumnDef("b", DataType.INT),
            ColumnDef("c", DataType.INT), ColumnDef("name", DataType.VARCHAR)))}
        self.reads = []

    def table_exists(self, name):
        self.reads.append(("exists", name))
        return name in self.schemas

    def get_schema(self, name):
        self.reads.append(("schema", name))
        return self.schemas[name]


@pytest.fixture
def catalog():
    return Catalog()


def test_shared_plan_contract_all_statements(catalog):
    plans = SQLCompiler().compile("CREATE TABLE u(id INT); INSERT INTO t(name,c,b,a) VALUES('中文; x',3,2,10+8); SELECT name,a FROM t WHERE a>=1; DELETE FROM t;", catalog)
    assert [type(p) for p in plans] == [CreateTablePlan, InsertPlan, ProjectPlan, DeletePlan]
    assert plans[0].columns == (ColumnDef("id", DataType.INT),)
    assert plans[1].columns == ("a", "b", "c", "name")
    assert plans[1].values == (18, 2, 3, "中文; x")
    assert plans[2].columns == ("name", "a")
    assert isinstance(plans[2].child, FilterPlan)
    assert isinstance(plans[2].child.predicate, BinaryExpr)
    assert isinstance(plans[2].child.predicate.left, IdentifierExpr)
    assert isinstance(plans[2].child.child, SeqScanPlan)
    assert isinstance(plans[3].child, SeqScanPlan)
    assert "u" not in catalog.schemas


def test_individual_stages_use_cpp(catalog):
    sql = "SELECT * FROM t WHERE TRUE AND a>10+8;"
    tokens = Lexer().tokenize(sql)
    parsed = Parser().parse(tokens)
    bound = SemanticAnalyzer().analyze(parsed, catalog)
    before = PlanGenerator().build(bound[0])
    after = PlanOptimizer().optimize(before)
    assert before.child.predicate.operator == "AND"
    assert after.child.predicate.operator == ">"
    assert after.child.predicate.right.value == 18
    assert after == SQLCompiler().compile(sql, catalog)[0]
    assert bound[0].predicate.data_type == DataType.BOOL
    assert bound[0].predicate.right.left.column_index == 0
    assert parsed[0].predicate.right.left.data_type is None


@pytest.mark.parametrize("sql,stage,code,line,column", [
    ("\nSELECT @;", "LEXICAL", "ILLEGAL_CHARACTER", 2, 8),
    ("\nSELECT * FROM t", "SYNTAX", "UNEXPECTED_TOKEN", 2, 16),
    ("\nSELECT missing FROM t;", "SEMANTIC", "COLUMN_NOT_FOUND", 2, 8),
    ("SELECT * FROM absent;", "SEMANTIC", "TABLE_NOT_FOUND", 1, 15),
    ("CREATE TABLE u(x FLOAT);", "SEMANTIC", "UNSUPPORTED_TYPE", 1, 18),
    ("CREATE TABLE u(x BOOL);", "SEMANTIC", "UNSUPPORTED_TYPE", 1, 18),
    ("SELECT * FROM t WHERE a='x';", "SEMANTIC", "TYPE_MISMATCH", 1, 24),
    ("INSERT INTO t VALUES(1);", "SEMANTIC", "VALUE_COUNT_MISMATCH", 1, 1),
    ("INSERT INTO t VALUES('x',2,3,'x');", "SEMANTIC", "TYPE_MISMATCH", 1, 22),
])
def test_diagnostics_are_shared_db_errors(catalog, sql, stage, code, line, column):
    with pytest.raises(DBError) as caught:
        SQLCompiler().compile(sql, catalog)
    error = caught.value
    assert error.stage == ErrorStage(stage)
    assert error.code == code
    assert (error.location.line, error.location.column) == (line, column)
    if stage == "SYNTAX":
        assert error.actual == "EOF"
        assert ";" in error.expected


def test_multiple_statements_preserve_absolute_locations(catalog):
    with pytest.raises(DBError) as caught:
        SQLCompiler().compile("SELECT a FROM t;\r\n/*中文*/ SELECT missing FROM t;", catalog)
    assert caught.value.code == "COLUMN_NOT_FOUND"
    assert (caught.value.location.line, caught.value.location.column) == (2, 15)


def test_multiline_strings_and_crlf(catalog):
    plans = SQLCompiler().compile("INSERT INTO t VALUES(1,2,3,'a\r\nb');\r\nSELECT a FROM t;", catalog)
    assert plans[0].values[-1] == "a\r\nb"
    assert plans[1].columns == ("a",)


@pytest.mark.parametrize("keyword", ["select", "SELECT", "SeLeCt"])
def test_precedence_and_names(keyword, catalog):
    detail = SQLCompiler().compile_detailed(f"{keyword} A FROM T WHERE a=1 OR b=2 AND NOT c=3;", catalog)[0]
    expr = detail.bound_ast.predicate
    assert expr.operator == "OR"
    assert expr.right.operator == "AND"
    assert isinstance(expr.right.right, UnaryExpr)
    assert expr.right.right.operator == "NOT"
    assert expr.right.right.operand.operator == "="
    assert expr.left.left.table_name == "t"
    assert detail.plan_after.columns == ("a",)
    assert detail.tokens[0]["lexeme"] == keyword


@pytest.mark.parametrize("where,rows", [
    ("TRUE AND a>10+8", [(1,2,3,"x"), (20,2,3,"x")]),
    ("FALSE OR a=1", [(1,2,3,"x"), (2,2,3,"x")]),
    ("NOT NOT a=1", [(1,2,3,"x"), (2,2,3,"x")]),
    ("a=1 OR b=2 AND NOT c=3", [(0,2,0,"x"), (0,2,3,"x"), (1,0,0,"x")]),
    ("FALSE AND a+9223372036854775807>0", [(1,2,3,"x")]),
    ("TRUE OR a+9223372036854775807>0", [(1,2,3,"x")]),
    ("a+9223372036854775807>0 AND FALSE", [(1,2,3,"x")]),
])
def test_optimization_preserves_value_or_error(catalog, where, rows):
    detail = SQLCompiler().compile_detailed("SELECT * FROM t WHERE " + where + ";", catalog)[0]
    before = detail.plan_before.child.predicate
    after = detail.plan_after.child.predicate if isinstance(detail.plan_after.child, FilterPlan) else LiteralExpr(True, DataType.BOOL)
    def outcome(expr, row):
        try:
            return evaluate_expression(expr, row)
        except DBError as error:
            return error.stage, error.code
    for row in rows:
        assert outcome(before, row) == outcome(after, row)


def test_insert_constants_materialized_without_optional_optimization(catalog):
    detail = SQLCompiler().compile_detailed("SELECT * FROM t WHERE TRUE AND a>10+8;", catalog, optimize=False)[0]
    assert detail.native["plan_before"] == detail.native["plan_after"]
    assert detail.plan_before == detail.plan_after
    plan = SQLCompiler().compile("INSERT INTO t VALUES(-9223372036854775808,10-3-2,2+3*4,'x');", catalog, optimize=False)[0]
    assert plan.values == (-9223372036854775808, 5, 14, "x")
    with pytest.raises(DBError, match="INTEGER_OVERFLOW"):
        SQLCompiler().compile("INSERT INTO t VALUES(9223372036854775807+1,2,3,'x');", catalog)


def test_compile_batch_never_invents_catalog_state(catalog):
    with pytest.raises(DBError, match="TABLE_NOT_FOUND"):
        SQLCompiler().compile("CREATE TABLE u(id INT); SELECT * FROM u;", catalog)
    assert "u" not in catalog.schemas


def test_execute_updates_real_catalog_before_next_bind(catalog):
    class Executor:
        def __init__(self):
            self.plans = []

        def execute(self, plan):
            self.plans.append(plan)
            if isinstance(plan, CreateTablePlan):
                catalog.schemas[plan.table_name] = TableSchema(plan.table_name, plan.columns)
            return ExecutionResult(message="ok")

    executor = Executor()
    results = SQLCompiler().compile_and_execute("CREATE TABLE u(id INT); INSERT INTO u VALUES(1); SELECT * FROM u; DELETE FROM u;", catalog, executor)
    assert len(results) == 4
    assert [type(p) for p in executor.plans] == [CreateTablePlan, InsertPlan, ProjectPlan, DeletePlan]
    assert executor.plans[1].values == (1,)


def test_failed_create_execution_stops_batch(catalog):
    class Executor:
        def execute(self, plan):
            raise DBError(ErrorStage.EXECUTION, "FAILED", "failure")
    with pytest.raises(DBError, match="FAILED"):
        SQLCompiler().compile_and_execute("CREATE TABLE u(id INT); SELECT * FROM u;", catalog, Executor())
    assert "u" not in catalog.schemas
    assert catalog.reads == [("exists", "u")]


def test_missing_binary_has_actionable_db_error(catalog, monkeypatch, tmp_path):
    monkeypatch.setenv("MINISQL_BRIDGE", str(tmp_path / "missing"))
    with pytest.raises(DBError, match="NATIVE_COMPILER_NOT_FOUND"):
        SQLCompiler().compile("SELECT * FROM t;", catalog)


def test_invalid_unicode_rejected(catalog):
    with pytest.raises(DBError, match="INVALID_UTF8"):
        SQLCompiler().compile("SELECT '\ud800';", catalog)


def test_details_serializable(catalog):
    detail = SQLCompiler().compile_detailed("SELECT * FROM t;", catalog)[0]
    assert json.loads(json.dumps(detail.native))["semantic"] == "PASS"
    assert asdict(detail.plan_after)["columns"] == ("a", "b", "c", "name")


def test_plan_only_in_memory_executor_acceptance(catalog):
    """Integration contract double, not the production/persistent engine."""
    class Executor:
        def __init__(self):
            self.tables = {}

        def scan(self, node):
            if isinstance(node, SeqScanPlan):
                return self.tables[node.table_name]
            assert isinstance(node, FilterPlan)
            return [row for row in self.scan(node.child) if evaluate_expression(node.predicate, row)]

        def execute(self, node):
            if isinstance(node, CreateTablePlan):
                catalog.schemas[node.table_name] = TableSchema(node.table_name, node.columns)
                self.tables[node.table_name] = []
                return ExecutionResult(message="created")
            if isinstance(node, InsertPlan):
                schema = catalog.get_schema(node.table_name)
                values = dict(zip(node.columns, node.values))
                self.tables[node.table_name].append([values[c.name] for c in schema.columns])
                return ExecutionResult(affected_rows=1)
            if isinstance(node, ProjectPlan):
                scan = node.child
                while isinstance(scan, FilterPlan):
                    scan = scan.child
                columns = [c.name for c in catalog.get_schema(scan.table_name).columns]
                indices = [columns.index(c) for c in node.columns]
                return ExecutionResult(columns=list(node.columns),
                                       rows=[[row[i] for i in indices] for row in self.scan(node.child)])
            assert isinstance(node, DeletePlan)
            selected = {id(row) for row in self.scan(node.child)}
            rows = self.tables[node.table_name]
            self.tables[node.table_name] = [row for row in rows if id(row) not in selected]
            return ExecutionResult(affected_rows=len(selected))

    from pathlib import Path
    sql = Path(__file__).with_name("e2e_demo.sql").read_text(encoding="utf-8")
    results = SQLCompiler().compile_and_execute(sql, catalog, Executor())
    assert results[4].rows == [[1, "Alice"], [3, "Carol"]]
    assert results[5].affected_rows == 1
    assert results[6].rows == [[2, "Bob", 17], [3, "Carol", 22]]
