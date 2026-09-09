"""Row serialization and slotted-page layout skeleton. Implementation owner: wzy."""

from typing import Any

from minidbms.common.types import TableSchema


class RecordCodec:
    def encode(self, schema: TableSchema, values: list[Any]) -> bytes:
        raise NotImplementedError("wzy: implement row serialization")

    def decode(self, schema: TableSchema, data: bytes) -> list[Any]:
        raise NotImplementedError("wzy: implement row deserialization")
