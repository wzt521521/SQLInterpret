"""Expression nodes shared by compiler and execution engine."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TypeAlias

from .types import DataType, SourceLocation

ScalarValue: TypeAlias = int | str | bool


@dataclass(frozen=True, slots=True)
class LiteralExpr:
    value: ScalarValue
    data_type: DataType
    location: SourceLocation | None = None


@dataclass(frozen=True, slots=True)
class IdentifierExpr:
    name: str
    location: SourceLocation | None = None


@dataclass(frozen=True, slots=True)
class UnaryExpr:
    operator: str
    operand: Expression
    location: SourceLocation | None = None


@dataclass(frozen=True, slots=True)
class BinaryExpr:
    left: Expression
    operator: str
    right: Expression
    location: SourceLocation | None = None


Expression: TypeAlias = LiteralExpr | IdentifierExpr | UnaryExpr | BinaryExpr
