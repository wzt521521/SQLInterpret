"""Parser skeleton. Implementation owner: zby."""

from .ast import Statement
from .token import Token


class Parser:
    def parse(self, tokens: list[Token]) -> list[Statement]:
        raise NotImplementedError("zby: implement SQL parsing and AST construction")
