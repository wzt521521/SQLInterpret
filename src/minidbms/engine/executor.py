"""Logical plan executor skeleton. Implementation owner: wzy."""

from minidbms.common.plans import PlanNode
from minidbms.common.types import ExecutionResult


class Executor:
    def execute(self, plan: PlanNode) -> ExecutionResult:
        raise NotImplementedError("wzy: implement logical plan execution")
