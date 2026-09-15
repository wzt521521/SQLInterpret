"""Evaluate bound WHERE expression trees without interpreting SQL text."""

from __future__ import annotations

from typing import Any

from minidbms.common.expressions import BinaryExpr, Expression, IdentifierExpr, LiteralExpr, UnaryExpr

from .errors import ExecutionError
from .record import INT64_MAX, INT64_MIN


def checked_int(value: int) -> int:
    if not INT64_MIN <= value <= INT64_MAX:
        raise ExecutionError("INTEGER_OVERFLOW", "signed 64-bit arithmetic overflow")
    return value


class ExpressionEvaluator:
    def evaluate(self, expression: Expression, row: dict[str, Any]) -> Any:
        if isinstance(expression, LiteralExpr):
            value = expression.value
            return checked_int(value) if type(value) is int else value
        if isinstance(expression, IdentifierExpr):
            name = expression.name.lower()
            if name not in row:
                raise ExecutionError("COLUMN_NOT_FOUND", f"column {name!r} is not available")
            return row[name]
        if isinstance(expression, UnaryExpr):
            value = self.evaluate(expression.operand, row)
            if expression.operator == "NOT":
                if type(value) is not bool:
                    raise ExecutionError("TYPE_MISMATCH", "NOT requires BOOL")
                return not value
            if type(value) is not int:
                raise ExecutionError("TYPE_MISMATCH", "unary arithmetic requires INT")
            if expression.operator == "+":
                return checked_int(value)
            if expression.operator == "-":
                return checked_int(-value)
        if isinstance(expression, BinaryExpr):
            operator = expression.operator.upper()
            left = self.evaluate(expression.left, row)
            if operator in ("AND", "OR"):
                if type(left) is not bool:
                    raise ExecutionError("TYPE_MISMATCH", f"{operator} requires BOOL")
                if operator == "AND" and not left:
                    return False
                if operator == "OR" and left:
                    return True
                right = self.evaluate(expression.right, row)
                if type(right) is not bool:
                    raise ExecutionError("TYPE_MISMATCH", f"{operator} requires BOOL")
                return right
            right = self.evaluate(expression.right, row)
            if type(left) is not type(right):
                raise ExecutionError("TYPE_MISMATCH", "expression operands have different types")
            if operator in ("+", "-", "*"):
                if type(left) is not int:
                    raise ExecutionError("TYPE_MISMATCH", "arithmetic requires INT")
                if operator == "+":
                    return checked_int(left + right)
                if operator == "-":
                    return checked_int(left - right)
                return checked_int(left * right)
            if operator in ("=", "!=", ">", ">=", "<", "<="):
                if type(left) is bool and operator not in ("=", "!="):
                    raise ExecutionError("TYPE_MISMATCH", "BOOL only supports equality comparisons")
                if type(left) not in (int, str, bool):
                    raise ExecutionError("TYPE_MISMATCH", "comparison operand type is unsupported")
                if type(left) is str:
                    left, right = left.encode("utf-8"), right.encode("utf-8")
                if operator == "=":
                    return left == right
                if operator == "!=":
                    return left != right
                if operator == ">":
                    return left > right
                if operator == ">=":
                    return left >= right
                if operator == "<":
                    return left < right
                return left <= right
        raise ExecutionError("UNSUPPORTED_EXPRESSION", f"unsupported expression {type(expression).__name__}")
