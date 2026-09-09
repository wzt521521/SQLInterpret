"""Lexical analyzer skeleton. Implementation owner: zby."""

from .token import Token


class Lexer:
    def tokenize(self, sql_text: str) -> list[Token]:
        raise NotImplementedError("zby: implement SQL tokenization")
