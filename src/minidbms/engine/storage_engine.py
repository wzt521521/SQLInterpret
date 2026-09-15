"""Map typed table rows to opaque pages through StorageManagerProtocol."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterator

from minidbms.common.interfaces import StorageManagerProtocol

from .catalog_manager import CatalogManager
from .errors import ExecutionError
from .record import RecordCodec, RowPage


@dataclass(frozen=True)
class RowRef:
    table_name: str
    page_id: int
    slot_id: int


class StorageEngine:
    def __init__(self, storage: StorageManagerProtocol, catalog: CatalogManager) -> None:
        self.storage = storage
        self.catalog = catalog
        self.codec = RecordCodec()

    def insert(self, table_name: str, values: list[Any]) -> RowRef:
        schema = self.catalog.get_schema(table_name)
        encoded = self.codec.encode(schema, values)
        if len(encoded) > RowPage.MAX_RECORD_SIZE:
            raise ExecutionError("RECORD_TOO_LARGE", "record cannot fit in one data page")
        for page_id in self.catalog.page_ids(table_name):
            page = RowPage(self.storage.read_page(page_id))
            if page.can_fit(len(encoded)):
                slot_id = page.insert(encoded)
                self.storage.write_page(page_id, page.to_bytes())
                return RowRef(schema.table_name, page_id, slot_id)

        page_id = self.storage.allocate_page()
        try:
            page = RowPage.empty()
            slot_id = page.insert(encoded)
            self.storage.write_page(page_id, page.to_bytes())
            self.storage.flush_page(page_id)
            self.catalog.add_page(table_name, page_id)
        except Exception:
            self.storage.free_page(page_id)
            raise
        return RowRef(schema.table_name, page_id, slot_id)

    def scan(self, table_name: str) -> Iterator[tuple[RowRef, dict[str, Any]]]:
        schema = self.catalog.get_schema(table_name)
        for page_id in self.catalog.page_ids(table_name):
            page = RowPage(self.storage.read_page(page_id))
            for slot_id, encoded in page.iter_live():
                values = self.codec.decode(schema, encoded)
                yield RowRef(schema.table_name, page_id, slot_id), {
                    column.name: value for column, value in zip(schema.columns, values)
                }

    def delete(self, refs: list[RowRef]) -> int:
        affected = 0
        for ref in refs:
            if ref.page_id not in self.catalog.page_ids(ref.table_name):
                raise ExecutionError("ROW_NOT_FOUND", "row reference is not part of the table")
            page = RowPage(self.storage.read_page(ref.page_id))
            if page.delete(ref.slot_id):
                self.storage.write_page(ref.page_id, page.to_bytes())
                affected += 1
        return affected
