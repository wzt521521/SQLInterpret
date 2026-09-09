"""Storage-specific errors."""

from minidbms.common.errors import DBError, ErrorStage


class StorageError(DBError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(ErrorStage.STORAGE, code, message)
