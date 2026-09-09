"""Semantic analyzer skeleton. Implementation owner: zby."""

from minidbms.common.interfaces import CatalogView

from .ast import Statement


class SemanticAnalyzer:
    def analyze(self, statements: list[Statement], catalog: CatalogView) -> None:
        raise NotImplementedError("zby: implement name binding and type checking")
