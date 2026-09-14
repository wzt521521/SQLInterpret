"""Persistent fixed-size disk page manager.

The first 4096 bytes form an internal file header.  Logical page zero starts
immediately after that header, so callers can never overwrite allocation
metadata through the normal page API.
"""

from __future__ import annotations

import os
from pathlib import Path
import struct
from threading import RLock
from typing import BinaryIO

from .errors import StorageError
from .page import PAGE_SIZE


class FileManager:
    """Own the database file and persist logical-page allocation state."""

    HEADER_SIZE = PAGE_SIZE
    MAGIC = b"MDBMSPG1"
    FORMAT_VERSION = 1
    _HEADER_PREFIX = struct.Struct("<8sIQ")
    _BITMAP_SIZE = HEADER_SIZE - _HEADER_PREFIX.size
    MAX_PAGES = _BITMAP_SIZE * 8

    def __init__(self, db_path: str | Path) -> None:
        self.db_path = Path(db_path)
        self._lock = RLock()
        self._file: BinaryIO | None = None
        self._page_count = 0
        self._allocation_bitmap = bytearray(self._BITMAP_SIZE)

        try:
            self.db_path.parent.mkdir(parents=True, exist_ok=True)
            exists = self.db_path.exists()
            self._file = self.db_path.open("r+b" if exists else "w+b")
            if exists:
                self._load_header()
            else:
                self._persist_header()
        except StorageError:
            self._close_after_failed_init()
            raise
        except OSError as error:
            self._close_after_failed_init()
            raise StorageError(
                "FILE_OPEN_FAILED", f"cannot open database file: {error}"
            ) from error

    @property
    def page_count(self) -> int:
        """Number of physical logical-page slots created so far."""
        with self._lock:
            self._ensure_open()
            return self._page_count

    def is_allocated(self, page_id: int) -> bool:
        """Return whether *page_id* names a currently allocated page."""
        with self._lock:
            self._ensure_open()
            self._validate_page_id_type(page_id)
            return page_id < self._page_count and self._bit_is_set(page_id)

    def allocate_page(self) -> int:
        """Allocate a zero-filled page, reusing the lowest free id first."""
        with self._lock:
            file = self._ensure_open()
            page_id = next(
                (
                    candidate
                    for candidate in range(self._page_count)
                    if not self._bit_is_set(candidate)
                ),
                self._page_count,
            )
            if page_id >= self.MAX_PAGES:
                raise StorageError(
                    "PAGE_LIMIT_EXCEEDED",
                    f"database supports at most {self.MAX_PAGES} pages",
                )

            extending = page_id == self._page_count
            old_page_count = self._page_count
            old_file_size = self.HEADER_SIZE + old_page_count * PAGE_SIZE
            try:
                file.seek(self._page_offset(page_id))
                file.write(bytes(PAGE_SIZE))
                if extending:
                    self._page_count += 1
                self._set_bit(page_id, True)
                self._persist_header()
            except StorageError:
                self._set_bit(page_id, False)
                self._page_count = old_page_count
                if extending:
                    try:
                        file.truncate(old_file_size)
                    except OSError:
                        pass
                raise
            except OSError as error:
                self._set_bit(page_id, False)
                self._page_count = old_page_count
                if extending:
                    try:
                        file.truncate(old_file_size)
                    except OSError:
                        pass
                raise StorageError(
                    "IO_ERROR", f"failed to allocate page {page_id}: {error}"
                ) from error
            return page_id

    def free_page(self, page_id: int) -> None:
        """Release a page, zeroing its old content before making it reusable."""
        with self._lock:
            file = self._ensure_open()
            self._require_allocated(page_id)
            try:
                file.seek(self._page_offset(page_id))
                file.write(bytes(PAGE_SIZE))
                self._set_bit(page_id, False)
                self._persist_header()
            except StorageError:
                self._set_bit(page_id, True)
                raise
            except OSError as error:
                self._set_bit(page_id, True)
                raise StorageError(
                    "IO_ERROR", f"failed to free page {page_id}: {error}"
                ) from error

    def read_page(self, page_id: int) -> bytes:
        """Read exactly one allocated page."""
        with self._lock:
            file = self._ensure_open()
            self._require_allocated(page_id)
            try:
                file.seek(self._page_offset(page_id))
                data = file.read(PAGE_SIZE)
            except OSError as error:
                raise StorageError(
                    "IO_ERROR", f"failed to read page {page_id}: {error}"
                ) from error
            if len(data) != PAGE_SIZE:
                raise StorageError(
                    "CORRUPT_FILE",
                    f"page {page_id} is truncated: expected {PAGE_SIZE} bytes, got {len(data)}",
                )
            return data

    def write_page(self, page_id: int, data: bytes) -> None:
        """Overwrite an allocated page, padding short byte strings with zeros."""
        with self._lock:
            file = self._ensure_open()
            self._require_allocated(page_id)
            normalized = self.normalize_page_data(data)
            try:
                file.seek(self._page_offset(page_id))
                file.write(normalized)
                self._sync_file()
            except OSError as error:
                raise StorageError(
                    "IO_ERROR", f"failed to write page {page_id}: {error}"
                ) from error

    def close(self) -> None:
        """Flush and close the file. Repeated calls are harmless."""
        with self._lock:
            if self._file is None:
                return
            file = self._file
            try:
                self._sync_file()
                file.close()
            except OSError as error:
                raise StorageError(
                    "IO_ERROR", f"failed to close database file: {error}"
                ) from error
            finally:
                self._file = None

    def __enter__(self) -> FileManager:
        self._ensure_open()
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()

    @classmethod
    def normalize_page_data(cls, data: bytes) -> bytes:
        if not isinstance(data, bytes):
            raise StorageError("INVALID_DATA_TYPE", "page data must be bytes")
        if len(data) > PAGE_SIZE:
            raise StorageError(
                "PAGE_DATA_TOO_LARGE",
                f"page data is {len(data)} bytes; maximum is {PAGE_SIZE}",
            )
        return data.ljust(PAGE_SIZE, b"\x00")

    def _load_header(self) -> None:
        file = self._ensure_open()
        try:
            file.seek(0, os.SEEK_END)
            actual_size = file.tell()
            if actual_size < self.HEADER_SIZE:
                raise StorageError(
                    "CORRUPT_FILE", "database file is smaller than its header"
                )
            file.seek(0)
            header = file.read(self.HEADER_SIZE)
        except OSError as error:
            raise StorageError(
                "IO_ERROR", f"failed to read file header: {error}"
            ) from error

        magic, version, page_count = self._HEADER_PREFIX.unpack_from(header)
        if magic != self.MAGIC:
            raise StorageError("CORRUPT_FILE", "database file magic does not match")
        if version != self.FORMAT_VERSION:
            raise StorageError(
                "UNSUPPORTED_FORMAT",
                f"database format {version} is not supported",
            )
        if page_count > self.MAX_PAGES:
            raise StorageError("CORRUPT_FILE", "page count exceeds header capacity")
        expected_size = self.HEADER_SIZE + page_count * PAGE_SIZE
        if actual_size != expected_size:
            raise StorageError(
                "CORRUPT_FILE",
                f"database file size is {actual_size}; expected {expected_size}",
            )

        self._page_count = page_count
        self._allocation_bitmap[:] = header[self._HEADER_PREFIX.size :]
        if self._has_bits_beyond_page_count():
            raise StorageError(
                "CORRUPT_FILE", "allocation bitmap references pages outside the file"
            )

    def _persist_header(self) -> None:
        file = self._ensure_open()
        header = bytearray(self.HEADER_SIZE)
        self._HEADER_PREFIX.pack_into(
            header, 0, self.MAGIC, self.FORMAT_VERSION, self._page_count
        )
        header[self._HEADER_PREFIX.size :] = self._allocation_bitmap
        try:
            file.seek(0)
            file.write(header)
            self._sync_file()
        except OSError as error:
            raise StorageError(
                "IO_ERROR", f"failed to persist file header: {error}"
            ) from error

    def _sync_file(self) -> None:
        file = self._ensure_open()
        file.flush()
        os.fsync(file.fileno())

    def _require_allocated(self, page_id: int) -> None:
        self._validate_page_id_type(page_id)
        if page_id >= self._page_count:
            raise StorageError("PAGE_NOT_FOUND", f"page {page_id} does not exist")
        if not self._bit_is_set(page_id):
            raise StorageError("PAGE_FREED", f"page {page_id} has been released")

    @staticmethod
    def _validate_page_id_type(page_id: int) -> None:
        if isinstance(page_id, bool) or not isinstance(page_id, int):
            raise StorageError("INVALID_PAGE_ID", "page id must be an integer")
        if page_id < 0:
            raise StorageError("INVALID_PAGE_ID", "page id cannot be negative")

    def _bit_is_set(self, page_id: int) -> bool:
        byte_index, bit_index = divmod(page_id, 8)
        return bool(self._allocation_bitmap[byte_index] & (1 << bit_index))

    def _set_bit(self, page_id: int, allocated: bool) -> None:
        byte_index, bit_index = divmod(page_id, 8)
        mask = 1 << bit_index
        if allocated:
            self._allocation_bitmap[byte_index] |= mask
        else:
            self._allocation_bitmap[byte_index] &= ~mask

    def _has_bits_beyond_page_count(self) -> bool:
        full_bytes, remaining_bits = divmod(self._page_count, 8)
        if remaining_bits:
            valid_mask = (1 << remaining_bits) - 1
            if self._allocation_bitmap[full_bytes] & ~valid_mask:
                return True
            full_bytes += 1
        return any(self._allocation_bitmap[full_bytes:])

    @staticmethod
    def _page_offset(page_id: int) -> int:
        return FileManager.HEADER_SIZE + page_id * PAGE_SIZE

    def _ensure_open(self) -> BinaryIO:
        if self._file is None:
            raise StorageError("STORAGE_CLOSED", "database file is closed")
        return self._file

    def _close_after_failed_init(self) -> None:
        if self._file is not None:
            try:
                self._file.close()
            finally:
                self._file = None
