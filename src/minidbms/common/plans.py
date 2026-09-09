"""Logical plan nodes produced by the compiler and consumed by the engine."""

from __future__ import annotations

from dataclasses import dataclass

from .expressions import Expression, ScalarValue
from .types import ColumnDef


class PlanNode:
    """Marker base class for all logical plan nodes."""


@dataclass(frozen=True, slots=True)
class CreateTablePlan(PlanNode):
    table_name: str
    columns: tuple[ColumnDef, ...]


@dataclass(frozen=True, slots=True)
class InsertPlan(PlanNode):
    table_name: str
    columns: tuple[str, ...]
    values: tuple[ScalarValue, ...]


@dataclass(frozen=True, slots=True)
class SeqScanPlan(PlanNode):
    table_name: str


@dataclass(frozen=True, slots=True)
class FilterPlan(PlanNode):
    predicate: Expression
    child: PlanNode


@dataclass(frozen=True, slots=True)
class ProjectPlan(PlanNode):
    columns: tuple[str, ...]
    child: PlanNode


@dataclass(frozen=True, slots=True)
class DeletePlan(PlanNode):
    table_name: str
    child: PlanNode
