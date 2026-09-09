"""Protocols that form the boundaries between team-owned modules."""

from __future__ import annotations

from pathlib import Path
from typing import Protocol

from .types import TableSchema


class CatalogView(Protocol):
    def table_exists(self, table_name: str) -> bool: ...

    def get_schema(self, table_name: str) -> TableSchema: ...


class StorageManagerProtocol(Protocol):
    PAGE_SIZE: int

    def allocate_page(self) -> int: ...

    def free_page(self, page_id: int) -> None: ...

    def read_page(self, page_id: int) -> bytes: ...

    def write_page(self, page_id: int, data: bytes) -> None: ...

    def flush_page(self, page_id: int) -> None: ...

    def flush_all(self) -> None: ...

    def stats(self) -> dict[str, int]: ...

    def close(self) -> None: ...


DatabasePath = str | Path
