"""Unified error contract for user-visible MiniDBMS failures."""

from __future__ import annotations

from enum import Enum

from .types import SourceLocation


class ErrorStage(str, Enum):
    LEXICAL = "LEXICAL"
    SYNTAX = "SYNTAX"
    SEMANTIC = "SEMANTIC"
    EXECUTION = "EXECUTION"
    STORAGE = "STORAGE"


class DBError(Exception):
    def __init__(
        self,
        stage: ErrorStage,
        code: str,
        message: str,
        location: SourceLocation | None = None,
    ) -> None:
        super().__init__(message)
        self.stage = stage
        self.code = code
        self.message = message
        self.location = location

    def __str__(self) -> str:
        position = ""
        if self.location is not None:
            position = f" at line {self.location.line}, column {self.location.column}"
        return f"{self.stage.value}:{self.code}{position}: {self.message}"
