"""Common value objects shared across compiler, storage, and engine modules."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any


class DataType(str, Enum):
    INT = "INT"
    VARCHAR = "VARCHAR"
    BOOL = "BOOL"


@dataclass(frozen=True, slots=True)
class SourceLocation:
    line: int
    column: int


@dataclass(frozen=True, slots=True)
class ColumnDef:
    name: str
    data_type: DataType


@dataclass(frozen=True, slots=True)
class TableSchema:
    table_name: str
    columns: tuple[ColumnDef, ...]


@dataclass(slots=True)
class ExecutionResult:
    columns: list[str] = field(default_factory=list)
    rows: list[list[Any]] = field(default_factory=list)
    affected_rows: int = 0
    message: str = ""
