"""Token definitions for the SQL lexer."""

from dataclasses import dataclass
from enum import Enum

from minidbms.common.types import SourceLocation


class TokenType(str, Enum):
    KEYWORD = "KEYWORD"
    IDENTIFIER = "IDENTIFIER"
    CONST = "CONST"
    OPERATOR = "OPERATOR"
    DELIMITER = "DELIMITER"
    EOF = "EOF"


@dataclass(frozen=True, slots=True)
class Token:
    type: TokenType
    lexeme: str
    location: SourceLocation
