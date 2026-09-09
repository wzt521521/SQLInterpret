"""Statement AST skeleton. Implementation owner: zby."""

from dataclasses import dataclass

from minidbms.common.expressions import Expression, ScalarValue
from minidbms.common.types import ColumnDef, SourceLocation


class Statement:
    """Marker base class for SQL statement nodes."""


@dataclass(frozen=True, slots=True)
class CreateTableStmt(Statement):
    table_name: str
    columns: tuple[ColumnDef, ...]
    location: SourceLocation


@dataclass(frozen=True, slots=True)
class InsertStmt(Statement):
    table_name: str
    columns: tuple[str, ...]
    values: tuple[ScalarValue, ...]
    location: SourceLocation


@dataclass(frozen=True, slots=True)
class SelectStmt(Statement):
    table_name: str
    columns: tuple[str, ...]
    predicate: Expression | None
    location: SourceLocation


@dataclass(frozen=True, slots=True)
class DeleteStmt(Statement):
    table_name: str
    predicate: Expression | None
    location: SourceLocation
