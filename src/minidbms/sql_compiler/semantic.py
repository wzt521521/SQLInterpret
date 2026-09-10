"""Bind against a read-only snapshot of the host CatalogView."""
from minidbms.common.interfaces import CatalogView
from minidbms.common.errors import ErrorStage
from .ast import Statement
from ._native import sql_request
from .errors import CompilerError
from .parser import statement_from_native


def compile_native(statement, catalog, optimize=True):
    if not statement.source:
        raise CompilerError(ErrorStage.SEMANTIC, "MISSING_SOURCE", "Bind statements returned by Parser.parse().")
    schemas = []
    if catalog.table_exists(statement.table_name):
        schema = catalog.get_schema(statement.table_name)
        if schema.table_name != statement.table_name:
            raise CompilerError(ErrorStage.SEMANTIC, "INVALID_CATALOG", "Catalog returned a different table name.", statement.location)
        schemas.append(schema)
    return sql_request("compile", statement.source, schemas, optimize)[0]


class SemanticAnalyzer:
    def analyze(self, statements: list[Statement], catalog: CatalogView) -> list[Statement]:
        """Return immutable bound copies; never register CREATE in the Catalog."""
        result = []
        for statement in statements:
            detail = compile_native(statement, catalog)
            bound = statement_from_native(detail["bound_ast"], statement.source)
            bound.native["plan_before"] = detail["plan_before"]
            bound.native["plan_after"] = detail["plan_after"]
            result.append(bound)
        return result
