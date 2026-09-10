"""Stable Python facade over zby's C++17 SQL compiler."""
from dataclasses import dataclass
from minidbms.common.interfaces import CatalogView
from minidbms.common.plans import PlanNode
from .ast import Statement
from .lexer import Lexer
from .parser import Parser, statement_from_native
from .semantic import compile_native
from ._convert import plan


@dataclass(frozen=True)
class CompilationResult:
    ast: Statement
    bound_ast: Statement
    plan_before: PlanNode
    plan_after: PlanNode
    tokens: list
    native: dict


class SQLCompiler:
    def prepare(self, sql_text: str) -> list[Statement]:
        return Parser().parse(Lexer().tokenize(sql_text))

    def compile_statement(self, statement: Statement, catalog: CatalogView,
                          optimize: bool = True) -> CompilationResult:
        detail = compile_native(statement, catalog, optimize)
        bound = statement_from_native(detail["bound_ast"], statement.source)
        bound.native["plan_before"] = detail["plan_before"]
        bound.native["plan_after"] = detail["plan_after"]
        before = plan(detail["plan_before"])
        after = plan(detail["plan_after"]) if optimize else before
        return CompilationResult(statement, bound, before, after, detail["tokens"], detail)

    def compile_detailed(self, sql_text: str, catalog: CatalogView,
                         optimize: bool = True) -> list[CompilationResult]:
        return [self.compile_statement(s, catalog, optimize) for s in self.prepare(sql_text)]

    def compile(self, sql_text: str, catalog: CatalogView,
                optimize: bool = True) -> list[PlanNode]:
        return [d.plan_after for d in self.compile_detailed(sql_text, catalog, optimize)]

    def compile_and_execute(self, sql_text, catalog, executor, optimize=True):
        """Execute each statement before binding the next against the real Catalog."""
        results = []
        for statement in self.prepare(sql_text):
            detail = self.compile_statement(statement, catalog, optimize)
            results.append(executor.execute(detail.plan_after))
        return results
