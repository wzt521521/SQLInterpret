"""Acceptance tests for zby's C++ compiler; build it before running pytest."""
import json
import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get("MINISQL_CLI", ROOT / "src" / "minidbms" / "sql_compiler" / "native" / "build" / ("minisql_cli.exe" if os.name == "nt" else "minisql_cli")))
SCHEMA = "CREATE TABLE t(a INT, b INT, c INT, name VARCHAR);"


def compile_sql(sql):
    result = subprocess.run(
        [str(EXE), "--sql", sql, "--format", "json"],
        capture_output=True, encoding="utf-8", timeout=10,
    )
    assert result.returncode in (0, 1), result.stderr
    payload = json.loads(result.stdout)
    assert (result.returncode == 0) == (payload["error"] is None)
    return payload


@pytest.mark.parametrize("keyword", ["select", "SELECT", "SeLeCt"])
def test_four_statements_and_case(keyword):
    result = compile_sql(SCHEMA + "INSERT INTO t VALUES(1,2,3,'Tom''s book');"
                         + f"{keyword} name,a FROM t WHERE a=1; DELETE FROM t;")
    assert result["error"] is None
    assert [r["plan_after"]["node"] for r in result["results"]] == [
        "CreateTablePlan", "InsertPlan", "ProjectPlan", "DeletePlan"]
    insert = result["results"][1]["plan_after"]
    assert insert["values"][3]["value"] == "Tom's book"
    assert result["results"][2]["plan_after"]["column_indices"] == [3, 0]


def test_precedence_and_binding():
    result = compile_sql(SCHEMA + "SELECT * FROM t WHERE a=1 OR b=2 AND NOT c=3;")
    detail = result["results"][-1]
    expr = detail["bound_ast"]["where"]
    assert expr["op"] == "OR"
    assert expr["right"]["op"] == "AND"
    assert expr["right"]["right"]["op"] == "NOT"
    assert expr["right"]["right"]["operand"]["op"] == "="
    assert expr["left"]["left"]["column_index"] == 0
    assert expr["left"]["left"]["data_type"] == "INT"
    assert expr["data_type"] == "BOOL"


def test_constant_folding_boolean_simplification_and_original_preserved():
    result = compile_sql(SCHEMA + "SELECT * FROM t WHERE TRUE AND a > 10+8;")
    detail = result["results"][-1]
    assert detail["plan_before"]["child"]["predicate"]["op"] == "AND"
    expr = detail["plan_after"]["child"]["predicate"]
    assert expr["op"] == ">"
    assert expr["right"]["value"] == 18


@pytest.mark.parametrize("sql,stage,code", [
    ("SELECT @ FROM t;", "LEXICAL", "ILLEGAL_CHARACTER"),
    ("INSERT INTO t VALUES ('oops);", "LEXICAL", "UNTERMINATED_STRING"),
    ("SELECT * FROM t WHERE a=12x;", "LEXICAL", "INVALID_NUMBER"),
    ("/* unterminated", "LEXICAL", "UNTERMINATED_COMMENT"),
    ("SELECT * FROM t", "SYNTAX", "UNEXPECTED_TOKEN"),
    ("SELECT * FROM t WHERE (a=1;", "SYNTAX", "UNEXPECTED_TOKEN"),
    ("SELECT * FROM missing;", "SEMANTIC", "TABLE_NOT_FOUND"),
    ("SELECT missing FROM t;", "SEMANTIC", "COLUMN_NOT_FOUND"),
    ("INSERT INTO t VALUES(1);", "SEMANTIC", "VALUE_COUNT_MISMATCH"),
    ("INSERT INTO t VALUES('bad',2,3,'x');", "SEMANTIC", "TYPE_MISMATCH"),
    ("INSERT INTO t(a,b,c) VALUES(1,2,3);", "SEMANTIC", "MISSING_COLUMN"),
    ("INSERT INTO t(a,a,c,name) VALUES(1,2,3,'x');", "SEMANTIC", "DUPLICATE_COLUMN"),
    ("SELECT * FROM t WHERE a AND TRUE;", "SEMANTIC", "TYPE_MISMATCH"),
    ("SELECT * FROM t WHERE a='x';", "SEMANTIC", "TYPE_MISMATCH"),
    ("CREATE TABLE t(x INT);", "SEMANTIC", "TABLE_ALREADY_EXISTS"),
    ("CREATE TABLE u(x INT,x VARCHAR);", "SEMANTIC", "DUPLICATE_COLUMN"),
    ("CREATE TABLE u(x BOOL);", "SEMANTIC", "UNSUPPORTED_TYPE"),
    ("SELECT * FROM t WHERE a=9223372036854775808;", "SEMANTIC", "INTEGER_OUT_OF_RANGE"),
    ("SELECT * FROM t WHERE FALSE AND missing=1;", "SEMANTIC", "COLUMN_NOT_FOUND"),
])
def test_errors(sql, stage, code):
    error = compile_sql(SCHEMA + "\n" + sql)["error"]
    assert (error["stage"], error["code"]) == (stage, code)
    assert error["location"]["line"] == 2
    assert error["location"]["column"] >= 1
    if stage == "SYNTAX":
        assert error["actual"]
        assert error["expected"]


def test_unicode_comments_and_location():
    result = compile_sql("-- comment\r\n" + SCHEMA + "\r\n/*中文*/ SELECT name FROM t WHERE name='中文';")
    assert result["error"] is None
    token = result["results"][-1]["tokens"][0]
    assert (token["line"], token["column"]) == (3, 8)
    error = compile_sql("-- hi\r\nSELECT @ FROM t;")["error"]
    assert error["location"] == {"line": 2, "column": 8}


def test_int64_min_and_left_associativity():
    result = compile_sql(SCHEMA + "INSERT INTO t VALUES(-9223372036854775808,10-3-2,2+3*4,'x');")
    assert result["error"] is None
    assert [v["value"] for v in result["results"][-1]["plan_after"]["values"]] == [
        -9223372036854775808, 5, 14, "x"]


def test_complex_input_is_rejected():
    error = compile_sql(SCHEMA + "SELECT * FROM t WHERE " + "NOT " * 200 + "TRUE;")["error"]
    assert (error["stage"], error["code"]) == ("SYNTAX", "EXPRESSION_TOO_COMPLEX")


def test_interactive_error_does_not_exit():
    result = subprocess.run([str(EXE)], input="SELECT @;\n" + SCHEMA + "\nquit\n",
                            capture_output=True, encoding="utf-8", timeout=10)
    assert result.returncode == 0
    assert "ILLEGAL_CHARACTER" in result.stdout
    assert "Semantic: PASS" in result.stdout


def test_ll1_table_has_no_conflicts():
    result = subprocess.run([str(EXE), "--ll1"], capture_output=True, encoding="utf-8", timeout=10)
    assert result.returncode == 0
    assert "no prediction conflicts" in result.stdout
    assert "FIRST(Program)" in result.stdout
    assert "FOLLOW(Program)" in result.stdout
