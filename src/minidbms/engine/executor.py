"""Execute shared logical plans against the persistent catalog and row pages."""

from __future__ import annotations

from typing import Any, Iterator

from minidbms.common.plans import (
    CreateTablePlan, DeletePlan, FilterPlan, InsertPlan, PlanNode, ProjectPlan, SeqScanPlan,
)
from minidbms.common.types import ExecutionResult, TableSchema

from .catalog_manager import CatalogManager
from .errors import ExecutionError
from .expression_eval import ExpressionEvaluator
from .storage_engine import RowRef, StorageEngine


class Executor:
    def __init__(self, catalog: CatalogManager, rows: StorageEngine) -> None:
        self.catalog = catalog
        self.rows = rows
        self.evaluator = ExpressionEvaluator()

    def execute(self, plan: PlanNode) -> ExecutionResult:
        if isinstance(plan, CreateTablePlan):
            self.catalog.create_table(TableSchema(plan.table_name, plan.columns))
            return ExecutionResult(message=f"table {plan.table_name} created")
        if isinstance(plan, InsertPlan):
            schema = self.catalog.get_schema(plan.table_name)
            names = [name.lower() for name in plan.columns]
            expected = {column.name for column in schema.columns}
            if len(names) != len(schema.columns) or len(names) != len(set(names)) or set(names) != expected or len(plan.values) != len(names):
                raise ExecutionError("VALUE_COUNT_MISMATCH", "INSERT columns and values must match the table schema")
            supplied = dict(zip(names, plan.values))
            values = [supplied[column.name] for column in schema.columns]
            self.rows.insert(plan.table_name, values)
            return ExecutionResult(affected_rows=1, message="1 row inserted")
        if isinstance(plan, DeletePlan):
            if self._table_name(plan.child).lower() != plan.table_name.lower():
                raise ExecutionError("INVALID_PLAN", "DELETE child scans another table")
            refs = [ref for ref, _ in self._scan(plan.child)]
            affected = self.rows.delete(refs)
            return ExecutionResult(affected_rows=affected, message=f"{affected} row(s) deleted")
        if isinstance(plan, (ProjectPlan, FilterPlan, SeqScanPlan)):
            table_name = self._table_name(plan)
            schema = self.catalog.get_schema(table_name)
            columns = (
                [column.name for column in schema.columns]
                if not isinstance(plan, ProjectPlan) or plan.columns == ("*",)
                else [column.lower() for column in plan.columns]
            )
            if any(column not in {item.name for item in schema.columns} for column in columns):
                raise ExecutionError("COLUMN_NOT_FOUND", "projection references a missing column")
            result_rows = [[row[column] for column in columns] for _, row in self._scan(
                plan.child if isinstance(plan, ProjectPlan) else plan
            )]
            return ExecutionResult(columns=columns, rows=result_rows, message=f"{len(result_rows)} row(s)")
        raise ExecutionError("INVALID_PLAN", f"unsupported plan {type(plan).__name__}")

    def _scan(self, plan: PlanNode) -> Iterator[tuple[RowRef, dict[str, Any]]]:
        if isinstance(plan, SeqScanPlan):
            yield from self.rows.scan(plan.table_name)
            return
        if isinstance(plan, FilterPlan):
            for ref, row in self._scan(plan.child):
                accepted = self.evaluator.evaluate(plan.predicate, row)
                if type(accepted) is not bool:
                    raise ExecutionError("TYPE_MISMATCH", "WHERE expression must evaluate to BOOL")
                if accepted:
                    yield ref, row
            return
        raise ExecutionError("INVALID_PLAN", "scan plan must be SeqScan or Filter")

    def _table_name(self, plan: PlanNode) -> str:
        if isinstance(plan, SeqScanPlan):
            return plan.table_name
        if isinstance(plan, (FilterPlan, ProjectPlan)):
            return self._table_name(plan.child)
        raise ExecutionError("INVALID_PLAN", "plan does not identify a table")
