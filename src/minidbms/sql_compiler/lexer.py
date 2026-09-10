"""Tokenize in C++, exposing the original shared Python token categories."""
from minidbms.common.types import SourceLocation
from ._native import sql_request
from .token import Token, TokenType


class Lexer:
    def tokenize(self, sql_text: str) -> list[Token]:
        result = []
        for token in sql_request("tokens", sql_text):
            kind = token["type"]
            if kind in ("EOF", "IDENTIFIER"):
                category = TokenType(kind)
            elif kind in ("INTEGER", "STRING", "TRUE", "FALSE"):
                category = TokenType.CONST
            elif kind in ("(", ")", ",", ";"):
                category = TokenType.DELIMITER
            elif kind in ("+", "-", "*", "=", "!=", "<", "<=", ">", ">="):
                category = TokenType.OPERATOR
            else:
                category = TokenType.KEYWORD
            result.append(Token(category, token["lexeme"],
                                SourceLocation(token["line"], token["column"])))
        return result
