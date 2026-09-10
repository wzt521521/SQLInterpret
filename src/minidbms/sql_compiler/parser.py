"""Python entry point for native table-driven LL(1) parsing."""
from minidbms.common.types import ColumnDef, DataType, SourceLocation
from minidbms.common.errors import ErrorStage
from .ast import CreateTableStmt, InsertStmt, SelectStmt, DeleteStmt, Statement
from .token import Token, TokenType
from .errors import CompilerError
from ._native import sql_request
from ._convert import expression


def source_from_tokens(tokens):
    """Reconstruct whitespace to retain source coordinates, including multiline strings."""
    result = []
    line = column = 1
    previous = ""
    for token in tokens:
        target = token.location
        if target.line < line or (target.line == line and target.column < column):
            raise CompilerError(ErrorStage.SYNTAX, "INVALID_TOKEN_STREAM", "Overlapping token locations.", target)
        if target.line > line:
            result.append("\n" * (target.line - line))
            line, column, previous = target.line, 1, ""
        result.append(" " * (target.column - column))
        column = target.column
        result.append(token.lexeme)
        for character in token.lexeme:
            if character == "\r":
                line, column = line + 1, 1
            elif character == "\n":
                line += previous != "\r"
                column = 1
            else:
                column += 1
            previous = character
    return "".join(result)


def statement_from_native(node, source):
    kwargs = dict(table_name=node["table_name"], location=SourceLocation(**node["location"]),
                  source=source, native=node)
    kind = node["node"]
    if kind == "CreateTableStmt":
        # Unsupported type names survive parsing and are rejected at SEMANTIC.
        columns = tuple(ColumnDef(c["name"], DataType(c["type_name"].upper())
                                  if c["type_name"].upper() in DataType._value2member_map_
                                  else c["type_name"]) for c in node["columns"])
        return CreateTableStmt(columns=columns, **kwargs)
    if kind == "InsertStmt":
        return InsertStmt(columns=tuple(c["name"] for c in node["columns"]),
                          values=tuple(expression(v) for v in node["values"]), **kwargs)
    if kind == "SelectStmt":
        columns = ("*",) if node["select_all"] and not node["schema"] else tuple(c["name"] for c in node["columns"])
        return SelectStmt(columns=columns, predicate=expression(node["where"]), **kwargs)
    return DeleteStmt(predicate=expression(node["where"]), **kwargs)


class Parser:
    def parse(self, tokens: list[Token]) -> list[Statement]:
        if not tokens or tokens[-1].type != TokenType.EOF or any(t.type == TokenType.EOF for t in tokens[:-1]):
            raise CompilerError(ErrorStage.SYNTAX, "INVALID_TOKEN_STREAM", "Expected one final EOF token.")
        source = source_from_tokens(tokens)
        nodes = sql_request("parse", source)
        statements = []
        start = 0
        for index, token in enumerate(tokens):
            if token.type == TokenType.DELIMITER and token.lexeme == ";":
                statements.append(statement_from_native(nodes[len(statements)], source_from_tokens(tokens[start:index + 1])))
                start = index + 1
        return statements
