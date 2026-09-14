"""Typed row codec and append-only slotted data pages."""

from __future__ import annotations

import struct
from typing import Any, Iterator

from minidbms.common.types import DataType, TableSchema
from minidbms.storage.page import PAGE_SIZE

from .errors import ExecutionError


INT64_MIN = -(1 << 63)
INT64_MAX = (1 << 63) - 1
ROW_MAGIC = b"MDBROW01"
ROW_VERSION = 1
ROW_HEADER = struct.Struct("<8sII")  # magic, version, slot count
SLOT = struct.Struct("<II")  # record offset, byte length with high tombstone bit
DELETED = 1 << 31


class RecordCodec:
    """INT is signed little-endian int64; VARCHAR is uint32 length + UTF-8."""

    def encode(self, schema: TableSchema, values: list[Any]) -> bytes:
        if len(values) != len(schema.columns):
            raise ExecutionError("VALUE_COUNT_MISMATCH", "record value count does not match schema")
        parts = []
        for column, value in zip(schema.columns, values):
            if column.data_type == DataType.INT:
                if type(value) is not int or not INT64_MIN <= value <= INT64_MAX:
                    raise ExecutionError("TYPE_MISMATCH", f"column {column.name!r} requires a signed 64-bit INT")
                parts.append(struct.pack("<q", value))
            elif column.data_type == DataType.VARCHAR:
                if type(value) is not str:
                    raise ExecutionError("TYPE_MISMATCH", f"column {column.name!r} requires VARCHAR")
                try:
                    encoded = value.encode("utf-8")
                except UnicodeEncodeError as error:
                    raise ExecutionError("INVALID_STRING", f"column {column.name!r} is not valid UTF-8") from error
                if len(encoded) > 0xFFFFFFFF:
                    raise ExecutionError("RECORD_TOO_LARGE", "VARCHAR exceeds its length prefix")
                parts.append(struct.pack("<I", len(encoded)) + encoded)
            else:
                raise ExecutionError("UNSUPPORTED_TYPE", f"column {column.name!r} has an unsupported type")
        return b"".join(parts)

    def decode(self, schema: TableSchema, data: bytes) -> list[Any]:
        values: list[Any] = []
        offset = 0
        for column in schema.columns:
            if column.data_type == DataType.INT:
                if offset + 8 > len(data):
                    raise ExecutionError("CORRUPT_RECORD", "INT record field is truncated")
                values.append(struct.unpack_from("<q", data, offset)[0])
                offset += 8
            elif column.data_type == DataType.VARCHAR:
                if offset + 4 > len(data):
                    raise ExecutionError("CORRUPT_RECORD", "VARCHAR length is truncated")
                size = struct.unpack_from("<I", data, offset)[0]
                offset += 4
                if offset + size > len(data):
                    raise ExecutionError("CORRUPT_RECORD", "VARCHAR payload is truncated")
                try:
                    values.append(data[offset:offset + size].decode("utf-8"))
                except UnicodeDecodeError as error:
                    raise ExecutionError("CORRUPT_RECORD", "VARCHAR contains invalid UTF-8") from error
                offset += size
            else:
                raise ExecutionError("UNSUPPORTED_TYPE", f"column {column.name!r} has an unsupported type")
        if offset != len(data):
            raise ExecutionError("CORRUPT_RECORD", "record contains unexpected trailing bytes")
        return values


class RowPage:
    """Slots grow forward; variable-length record payloads grow backward."""

    MAX_RECORD_SIZE = PAGE_SIZE - ROW_HEADER.size - SLOT.size

    def __init__(self, data: bytes) -> None:
        if len(data) != PAGE_SIZE:
            raise ExecutionError("CORRUPT_PAGE", "row page must contain exactly 4096 bytes")
        self._data = bytearray(data)
        magic, version, self.slot_count = ROW_HEADER.unpack_from(data)
        if magic != ROW_MAGIC or version != ROW_VERSION:
            raise ExecutionError("CORRUPT_PAGE", "row page magic or version is invalid")
        if self.slot_count > (PAGE_SIZE - ROW_HEADER.size) // SLOT.size:
            raise ExecutionError("CORRUPT_PAGE", "row page slot count is invalid")
        boundary = ROW_HEADER.size + self.slot_count * SLOT.size
        intervals = []
        for slot_id in range(self.slot_count):
            offset, marked_length = SLOT.unpack_from(data, ROW_HEADER.size + slot_id * SLOT.size)
            length = marked_length & ~DELETED
            if not length or offset < boundary or offset + length > PAGE_SIZE:
                raise ExecutionError("CORRUPT_PAGE", "row page contains an invalid slot")
            intervals.append((offset, offset + length))
        intervals.sort()
        if any(left[1] > right[0] for left, right in zip(intervals, intervals[1:])):
            raise ExecutionError("CORRUPT_PAGE", "row page records overlap")
        self._free_end = intervals[0][0] if intervals else PAGE_SIZE

    @classmethod
    def empty(cls) -> RowPage:
        data = bytearray(PAGE_SIZE)
        ROW_HEADER.pack_into(data, 0, ROW_MAGIC, ROW_VERSION, 0)
        return cls(bytes(data))

    def can_fit(self, record_size: int) -> bool:
        return ROW_HEADER.size + (self.slot_count + 1) * SLOT.size + record_size <= self._free_end

    def insert(self, record: bytes) -> int:
        if not record or len(record) > self.MAX_RECORD_SIZE:
            raise ExecutionError("RECORD_TOO_LARGE", "record cannot fit in a 4096-byte page")
        if not self.can_fit(len(record)):
            raise ExecutionError("PAGE_FULL", "row page has no space for this record")
        self._free_end -= len(record)
        self._data[self._free_end:self._free_end + len(record)] = record
        slot_id = self.slot_count
        SLOT.pack_into(self._data, ROW_HEADER.size + slot_id * SLOT.size, self._free_end, len(record))
        self.slot_count += 1
        ROW_HEADER.pack_into(self._data, 0, ROW_MAGIC, ROW_VERSION, self.slot_count)
        return slot_id

    def iter_live(self) -> Iterator[tuple[int, bytes]]:
        for slot_id in range(self.slot_count):
            offset, marked_length = SLOT.unpack_from(self._data, ROW_HEADER.size + slot_id * SLOT.size)
            if not marked_length & DELETED:
                yield slot_id, bytes(self._data[offset:offset + marked_length])

    def delete(self, slot_id: int) -> bool:
        if type(slot_id) is not int or not 0 <= slot_id < self.slot_count:
            raise ExecutionError("ROW_NOT_FOUND", f"slot {slot_id} does not exist")
        slot_offset = ROW_HEADER.size + slot_id * SLOT.size
        offset, marked_length = SLOT.unpack_from(self._data, slot_offset)
        if marked_length & DELETED:
            return False
        SLOT.pack_into(self._data, slot_offset, offset, marked_length | DELETED)
        return True

    def to_bytes(self) -> bytes:
        return bytes(self._data)
