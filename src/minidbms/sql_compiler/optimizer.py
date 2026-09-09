"""Rule-based logical plan optimizer skeleton. Implementation owner: zby."""

from minidbms.common.plans import PlanNode


class PlanOptimizer:
    def optimize(self, plan: PlanNode) -> PlanNode:
        raise NotImplementedError("zby: implement constant folding and boolean simplification")
