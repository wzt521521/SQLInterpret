"""Python statement views of the authoritative C++ AST."""

from dataclasses import dataclass, field

from minidbms.common.expressions import Expression
from minidbms.common.types import ColumnDef, SourceLocation


@dataclass(frozen=True, slots=True, kw_only=True)
class Statement:
    """Source preserves locations for native binding; native contains type metadata."""
    source: str = field(default="", repr=False, compare=False)
    native: dict = field(default_factory=dict, repr=False, compare=False)


@dataclass(frozen=True, slots=True)
class CreateTableStmt(Statement):
    table_name: str
    columns: tuple[ColumnDef, ...]
    location: SourceLocation


@dataclass(frozen=True, slots=True)
class InsertStmt(Statement):
    table_name: str
    columns: tuple[str, ...]
    values: tuple[Expression, ...]
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
