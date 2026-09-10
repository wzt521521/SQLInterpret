"""Expose native logical plans as the team's shared Python PlanNode objects."""
from minidbms.common.plans import PlanNode
from minidbms.common.errors import ErrorStage
from .ast import Statement
from ._convert import plan
from .errors import CompilerError


class PlanGenerator:
    def build(self, statement: Statement) -> PlanNode:
        if "plan_before" not in statement.native:
            raise CompilerError(ErrorStage.SEMANTIC, "UNBOUND_STATEMENT", "Use the bound statement returned by SemanticAnalyzer.analyze().", statement.location)
        return plan(statement.native["plan_before"])
