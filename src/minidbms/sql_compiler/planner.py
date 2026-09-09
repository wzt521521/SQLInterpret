"""AST-to-plan generator skeleton. Implementation owner: zby."""

from minidbms.common.plans import PlanNode

from .ast import Statement


class PlanGenerator:
    def build(self, statement: Statement) -> PlanNode:
        raise NotImplementedError("zby: implement logical plan generation")
