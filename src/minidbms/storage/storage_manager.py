"""Public page-storage facade used by the database engine."""

from __future__ import annotations

from pathlib import Path
from threading import RLock

from .buffer import BufferPool
from .errors import StorageError
from .file_manager import FileManager
from .page import PAGE_SIZE


class StorageManager:
    """Own a database file and its cache through the shared page/bytes API."""

    PAGE_SIZE = PAGE_SIZE

    def __init__(
        self,
        db_path: str | Path,
        buffer_capacity: int = 16,
        replacement_policy: str = "LRU",
    ) -> None:
        self.db_path = Path(db_path)
        self._lock = RLock()
        self._closed = False
        self._file_manager = FileManager(self.db_path)
        try:
            self._buffer_pool = BufferPool(
                buffer_capacity, replacement_policy, self._file_manager
            )
        except Exception:
            self._file_manager.close()
            raise

    def allocate_page(self) -> int:
        with self._lock:
            self._ensure_open()
            return self._file_manager.allocate_page()

    def free_page(self, page_id: int) -> None:
        with self._lock:
            self._ensure_open()
            self._buffer_pool.free_page(page_id)

    def read_page(self, page_id: int) -> bytes:
        with self._lock:
            self._ensure_open()
            return self._buffer_pool.read_page(page_id)

    def write_page(self, page_id: int, data: bytes) -> None:
        with self._lock:
            self._ensure_open()
            self._buffer_pool.write_page(page_id, data)

    def flush_page(self, page_id: int) -> None:
        with self._lock:
            self._ensure_open()
            self._buffer_pool.flush_page(page_id)

    def flush_all(self) -> None:
        with self._lock:
            self._ensure_open()
            self._buffer_pool.flush_all()

    def stats(self) -> dict[str, int]:
        with self._lock:
            self._ensure_open()
            return self._buffer_pool.stats()

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._buffer_pool.flush_all()
            self._file_manager.close()
            self._closed = True

    def __enter__(self) -> StorageManager:
        with self._lock:
            self._ensure_open()
            return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()

    def _ensure_open(self) -> None:
        if self._closed:
            raise StorageError("STORAGE_CLOSED", "storage manager is closed")
