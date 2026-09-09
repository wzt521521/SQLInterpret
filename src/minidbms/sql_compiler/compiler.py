"""Public compiler facade. Implementation owner: zby."""

from minidbms.common.interfaces import CatalogView
from minidbms.common.plans import PlanNode


class SQLCompiler:
    def compile(self, sql_text: str, catalog: CatalogView) -> list[PlanNode]:
        raise NotImplementedError("zby: connect Lexer, Parser, SemanticAnalyzer, and Planner")
