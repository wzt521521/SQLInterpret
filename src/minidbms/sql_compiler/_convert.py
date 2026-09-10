"""Convert native trees to shared Python classes without changing common/."""
from dataclasses import dataclass

from minidbms.common.expressions import LiteralExpr, IdentifierExpr, UnaryExpr, BinaryExpr
from minidbms.common.plans import (CreateTablePlan, InsertPlan, SeqScanPlan,
                                  FilterPlan, ProjectPlan, DeletePlan)
from minidbms.common.types import ColumnDef, DataType, SourceLocation
from minidbms.common.errors import ErrorStage
from .errors import CompilerError
from ._native import Writer, request


@dataclass(frozen=True, slots=True)
class BoundIdentifierExpr(IdentifierExpr):
    data_type: DataType | None = None
    table_name: str = ""
    column_index: int | None = None


@dataclass(frozen=True, slots=True)
class TypedUnaryExpr(UnaryExpr):
    data_type: DataType | None = None


@dataclass(frozen=True, slots=True)
class TypedBinaryExpr(BinaryExpr):
    data_type: DataType | None = None


def expression(node):
    if node is None:
        return None
    location = SourceLocation(**node["location"])
    dtype = DataType(node["data_type"]) if node["data_type"] != "UNBOUND" else None
    kind = node["node"]
    if kind == "LiteralExpr":
        # Unbound magnitudes remain exact Python integers; C++ validates INT64
        # range when binding, including the unary-minus spelling of INT64_MIN.
        value = int(node["integer_text"]) if "integer_text" in node else node["value"]
        if dtype is None:
            dtype = scalar_type(value)
        return LiteralExpr(value, dtype, location)
    if kind == "IdentifierExpr":
        return BoundIdentifierExpr(node["name"], location, dtype,
                                   node["table_name"], node["column_index"])
    if kind == "UnaryExpr":
        return TypedUnaryExpr(node["op"], expression(node["operand"]), location, dtype)
    if kind == "BinaryExpr":
        return TypedBinaryExpr(expression(node["left"]), node["op"],
                               expression(node["right"]), location, dtype)
    raise ValueError(f"Unknown native expression: {kind}")


def scalar_type(value):
    if type(value) is bool:
        return DataType.BOOL
    if type(value) is int:
        return DataType.INT
    if type(value) is str:
        return DataType.VARCHAR
    raise TypeError("Expected INT, VARCHAR or BOOL scalar")


def write_value(writer, value):
    dtype = scalar_type(value)
    writer.string(dtype.value)
    writer.string(str(value).lower() if dtype == DataType.BOOL else str(value))


def write_expression(writer, expr):
    dtype = getattr(expr, "data_type", None)
    if isinstance(expr, LiteralExpr):
        kind = "LiteralExpr"
    elif isinstance(expr, IdentifierExpr):
        kind = "IdentifierExpr"
        if dtype is None or getattr(expr, "column_index", None) is None:
            raise CompilerError(ErrorStage.SEMANTIC, "UNBOUND_EXPRESSION", "Identifier must be semantically bound.", expr.location)
    elif isinstance(expr, UnaryExpr):
        kind = "UnaryExpr"
        dtype = dtype or (DataType.BOOL if expr.operator == "NOT" else DataType.INT)
    elif isinstance(expr, BinaryExpr):
        kind = "BinaryExpr"
        dtype = dtype or (DataType.INT if expr.operator in ("+", "-", "*") else DataType.BOOL)
    else:
        raise TypeError("Unsupported expression")
    writer.string(kind)
    writer.string(dtype.value)
    location = expr.location or SourceLocation(1, 1)
    writer.number(location.line)
    writer.number(location.column)
    if isinstance(expr, LiteralExpr):
        write_value(writer, expr.value)
    elif isinstance(expr, IdentifierExpr):
        writer.string(expr.name)
        writer.string(expr.table_name)
        writer.number(expr.column_index)
    elif isinstance(expr, UnaryExpr):
        writer.string(expr.operator)
        write_expression(writer, expr.operand)
    else:
        writer.string(expr.operator)
        write_expression(writer, expr.left)
        write_expression(writer, expr.right)


def evaluate_expression(expr, row=()):
    """Optional executor helper: full schema-order row, checked INT64, short circuit."""
    writer = Writer()
    write_expression(writer, expr)
    writer.number(len(row))
    for value in row:
        write_value(writer, value)
    return request("evaluate", writer)["value"]


def plan(node):
    kind = node["node"]
    if kind == "CreateTablePlan":
        schema = node["schema"]
        return CreateTablePlan(schema["table_name"], tuple(
            ColumnDef(c["name"], DataType(c["data_type"])) for c in schema["columns"]))
    if kind == "InsertPlan":
        # Public InsertPlan requires scalar values. Materialize constant VALUES
        # with the same C++ checked arithmetic even with optimization disabled.
        values = []
        for value in node["values"]:
            expr = expression(value)
            values.append(expr.value if isinstance(expr, LiteralExpr) else evaluate_expression(expr))
        return InsertPlan(node["table_name"], tuple(node["columns"]), tuple(values))
    if kind == "SeqScanPlan":
        return SeqScanPlan(node["table_name"])
    if kind == "FilterPlan":
        return FilterPlan(expression(node["predicate"]), plan(node["child"]))
    if kind == "ProjectPlan":
        return ProjectPlan(tuple(node["columns"]), plan(node["child"]))
    if kind == "DeletePlan":
        return DeletePlan(node["table_name"], plan(node["child"]))
    raise ValueError(f"Unknown native plan: {kind}")


def write_plan(writer, node):
    writer.string(type(node).__name__)
    if isinstance(node, CreateTablePlan):
        writer.string(node.table_name)
        writer.number(len(node.columns))
        for column in node.columns:
            writer.string(column.name)
            writer.string(column.data_type.value)
    elif isinstance(node, InsertPlan):
        writer.string(node.table_name)
        writer.strings(node.columns)
        writer.number(len(node.values))
        for value in node.values:
            write_expression(writer, LiteralExpr(value, scalar_type(value)))
    elif isinstance(node, SeqScanPlan):
        writer.string(node.table_name)
    elif isinstance(node, FilterPlan):
        write_expression(writer, node.predicate)
        write_plan(writer, node.child)
    elif isinstance(node, ProjectPlan):
        writer.strings(node.columns)
        write_plan(writer, node.child)
    elif isinstance(node, DeletePlan):
        writer.string(node.table_name)
        write_plan(writer, node.child)
    else:
        raise TypeError("Unsupported logical plan")
