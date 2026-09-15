"""Small public orchestration API for the complete MiniDBMS pipeline."""

from __future__ import annotations

from pathlib import Path

from minidbms.common.types import ExecutionResult
from minidbms.sql_compiler import SQLCompiler
from minidbms.storage import StorageManager

from .catalog_manager import CatalogManager
from .executor import Executor
from .storage_engine import StorageEngine


class Database:
    def __init__(self, path: str | Path, buffer_capacity: int = 16, replacement_policy: str = "LRU") -> None:
        self.storage = StorageManager(path, buffer_capacity, replacement_policy)
        try:
            self.catalog = CatalogManager(self.storage)
            self.rows = StorageEngine(self.storage, self.catalog)
            self.executor = Executor(self.catalog, self.rows)
            self.compiler = SQLCompiler()
        except Exception:
            self.storage.close()
            raise

    def execute(self, sql_text: str) -> list[ExecutionResult]:
        return self.compiler.compile_and_execute(sql_text, self.catalog, self.executor)

    def stats(self) -> dict[str, int]:
        return self.storage.stats()

    def close(self) -> None:
        self.storage.close()

    def __enter__(self) -> Database:
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()
