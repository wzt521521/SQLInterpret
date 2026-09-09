"""WHERE expression evaluator skeleton. Implementation owner: wzy."""

from typing import Any

from minidbms.common.expressions import Expression


class ExpressionEvaluator:
    def evaluate(self, expression: Expression, row: dict[str, Any]) -> Any:
        raise NotImplementedError("wzy: implement typed expression evaluation")
