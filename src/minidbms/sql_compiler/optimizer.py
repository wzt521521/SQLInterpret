"""Apply the C++ optimizer to shared Python plans."""
from minidbms.common.plans import PlanNode
from ._native import Writer, request
from ._convert import write_plan, plan


class PlanOptimizer:
    def optimize(self, node: PlanNode) -> PlanNode:
        writer = Writer()
        write_plan(writer, node)
        return plan(request("optimize", writer))
