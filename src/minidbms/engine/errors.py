"""Execution and database-format errors visible through the shared DBError API."""

from minidbms.common.errors import DBError, ErrorStage


class ExecutionError(DBError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(ErrorStage.EXECUTION, code, message)
