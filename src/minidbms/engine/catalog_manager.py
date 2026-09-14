"""The sole persistent source of table schemas and table-to-page mappings."""

from __future__ import annotations

from dataclasses import dataclass
import json
import logging
import struct
import zlib

from minidbms.common.interfaces import StorageManagerProtocol
from minidbms.common.types import ColumnDef, DataType, TableSchema
from minidbms.storage.errors import StorageError

from .errors import ExecutionError


logger = logging.getLogger(__name__)
ROOT_MAGIC = b"MDBCAT01"
CHUNK_MAGIC = b"MDBCTCH1"
FORMAT_VERSION = 1
NO_PAGE = 0xFFFFFFFF
ROOT_HEADER = struct.Struct("<8sIIII")  # magic, version, first chunk, length, crc32
CHUNK_HEADER = struct.Struct("<8sII")  # magic, next chunk, payload length


@dataclass(frozen=True)
class TableInfo:
    schema: TableSchema
    pages: tuple[int, ...] = ()


class CatalogManager:
    """Store a copy-on-write catalog snapshot behind fixed logical page zero."""

    def __init__(self, storage: StorageManagerProtocol) -> None:
        self.storage = storage
        self._tables: dict[str, TableInfo] = {}
        self._snapshot_pages: tuple[int, ...] = ()
        try:
            root = storage.read_page(0)
        except StorageError as error:
            if error.code != "PAGE_NOT_FOUND":
                raise
            if storage.allocate_page() != 0:
                raise ExecutionError("CATALOG_ROOT_MISSING", "new database did not allocate catalog page zero")
            storage.write_page(0, ROOT_HEADER.pack(ROOT_MAGIC, FORMAT_VERSION, NO_PAGE, 0, 0))
            storage.flush_page(0)
        else:
            self._load(root)

    @staticmethod
    def _name(name: str) -> str:
        return name.lower()

    def table_exists(self, table_name: str) -> bool:
        return self._name(table_name) in self._tables

    def get_schema(self, table_name: str) -> TableSchema:
        return self._info(table_name).schema

    def page_ids(self, table_name: str) -> tuple[int, ...]:
        return self._info(table_name).pages

    def create_table(self, schema: TableSchema) -> None:
        name = self._name(schema.table_name)
        if name in self._tables:
            raise ExecutionError("TABLE_EXISTS", f"table {name!r} already exists")
        if not schema.columns:
            raise ExecutionError("EMPTY_SCHEMA", "a table needs at least one column")
        columns = tuple(ColumnDef(self._name(c.name), c.data_type) for c in schema.columns)
        names = [column.name for column in columns]
        if len(names) != len(set(names)):
            raise ExecutionError("DUPLICATE_COLUMN", f"table {name!r} has duplicate columns")
        if any(column.data_type not in (DataType.INT, DataType.VARCHAR) for column in columns):
            raise ExecutionError("UNSUPPORTED_TYPE", "table columns must be INT or VARCHAR")
        updated = dict(self._tables)
        updated[name] = TableInfo(TableSchema(name, columns))
        self._persist(updated)

    def add_page(self, table_name: str, page_id: int) -> None:
        name = self._name(table_name)
        info = self._info(name)
        if page_id <= 0 or page_id in self._snapshot_pages or any(
            page_id in item.pages for item in self._tables.values()
        ):
            raise ExecutionError("INVALID_DATA_PAGE", f"page {page_id} cannot be assigned to {name!r}")
        updated = dict(self._tables)
        updated[name] = TableInfo(info.schema, (*info.pages, page_id))
        self._persist(updated)

    def _info(self, name: str) -> TableInfo:
        normalized = self._name(name)
        if normalized not in self._tables:
            raise ExecutionError("TABLE_NOT_FOUND", f"table {normalized!r} does not exist")
        return self._tables[normalized]

    def _load(self, root: bytes) -> None:
        if len(root) != self.storage.PAGE_SIZE or len(root) < ROOT_HEADER.size:
            raise ExecutionError("CORRUPT_CATALOG", "catalog root has an invalid size")
        magic, version, first, size, checksum = ROOT_HEADER.unpack_from(root)
        if magic != ROOT_MAGIC or version != FORMAT_VERSION:
            raise ExecutionError("CORRUPT_CATALOG", "catalog root magic or version is invalid")
        if size == 0:
            if first != NO_PAGE or checksum != 0:
                raise ExecutionError("CORRUPT_CATALOG", "empty catalog has an invalid root pointer")
            return
        if first in (0, NO_PAGE):
            raise ExecutionError("CORRUPT_CATALOG", "catalog snapshot pointer is invalid")
        raw, pages = self._read_snapshot(first, size)
        if zlib.crc32(raw) != checksum:
            raise ExecutionError("CORRUPT_CATALOG", "catalog checksum does not match")
        try:
            decoded = json.loads(raw.decode("utf-8"))
            if set(decoded) != {"tables"} or not isinstance(decoded["tables"], list):
                raise ValueError("invalid catalog document")
            tables: dict[str, TableInfo] = {}
            used_pages = {0, *pages}
            for item in decoded["tables"]:
                name = item["name"]
                columns = item["columns"]
                data_pages = item["pages"]
                if not isinstance(name, str) or not name or name != name.lower() or name in tables:
                    raise ValueError("duplicate or invalid table name")
                if not isinstance(columns, list) or not columns or not isinstance(data_pages, list):
                    raise ValueError("invalid table metadata")
                schema_columns = tuple(ColumnDef(column[0], DataType(column[1])) for column in columns)
                if any(not isinstance(c.name, str) or not c.name or c.name != c.name.lower()
                       or c.data_type not in (DataType.INT, DataType.VARCHAR) for c in schema_columns):
                    raise ValueError("invalid column metadata")
                if len({c.name for c in schema_columns}) != len(schema_columns):
                    raise ValueError("duplicate column")
                for page_id in data_pages:
                    if type(page_id) is not int or page_id <= 0 or page_id in used_pages:
                        raise ValueError("duplicate or invalid data page")
                    used_pages.add(page_id)
                tables[name] = TableInfo(TableSchema(name, schema_columns), tuple(data_pages))
        except (ValueError, TypeError, KeyError, IndexError) as error:
            raise ExecutionError("CORRUPT_CATALOG", f"catalog metadata is invalid: {error}") from error
        self._tables = tables
        self._snapshot_pages = pages

    def _read_snapshot(self, first: int, size: int) -> tuple[bytes, tuple[int, ...]]:
        remaining = size
        chunks = []
        pages = []
        page_id = first
        while remaining:
            if page_id in (0, NO_PAGE) or page_id in pages:
                raise ExecutionError("CORRUPT_CATALOG", "catalog chain has a cycle or invalid page")
            pages.append(page_id)
            try:
                page = self.storage.read_page(page_id)
            except StorageError as error:
                raise ExecutionError("CORRUPT_CATALOG", f"catalog chunk {page_id} cannot be read: {error}") from error
            magic, next_page, length = CHUNK_HEADER.unpack_from(page)
            if magic != CHUNK_MAGIC or length == 0 or length > min(remaining, self.storage.PAGE_SIZE - CHUNK_HEADER.size):
                raise ExecutionError("CORRUPT_CATALOG", f"catalog chunk {page_id} is invalid")
            chunks.append(page[CHUNK_HEADER.size:CHUNK_HEADER.size + length])
            remaining -= length
            page_id = next_page
        if page_id != NO_PAGE:
            raise ExecutionError("CORRUPT_CATALOG", "catalog chain is longer than its root length")
        return b"".join(chunks), tuple(pages)

    def _persist(self, tables: dict[str, TableInfo]) -> None:
        document = {"tables": [
            {"name": name,
             "columns": [[column.name, column.data_type.value] for column in info.schema.columns],
             "pages": list(info.pages)}
            for name, info in sorted(tables.items())
        ]}
        raw = json.dumps(document, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        chunk_capacity = self.storage.PAGE_SIZE - CHUNK_HEADER.size
        chunks = [raw[i:i + chunk_capacity] for i in range(0, len(raw), chunk_capacity)]
        old_root = self.storage.read_page(0)
        old_pages = self._snapshot_pages
        new_pages: list[int] = []
        committed = False
        try:
            for _ in chunks:
                new_pages.append(self.storage.allocate_page())
            for index, chunk in enumerate(chunks):
                next_page = new_pages[index + 1] if index + 1 < len(new_pages) else NO_PAGE
                self.storage.write_page(new_pages[index], CHUNK_HEADER.pack(CHUNK_MAGIC, next_page, len(chunk)) + chunk)
                self.storage.flush_page(new_pages[index])
            root = ROOT_HEADER.pack(ROOT_MAGIC, FORMAT_VERSION, new_pages[0], len(raw), zlib.crc32(raw))
            self.storage.write_page(0, root)
            self.storage.flush_page(0)
            committed = True
        except Exception:
            if not committed:
                try:
                    self.storage.write_page(0, old_root)
                    self.storage.flush_page(0)
                finally:
                    for page_id in new_pages:
                        try:
                            self.storage.free_page(page_id)
                        except StorageError:
                            logger.warning("could not release uncommitted catalog page %s", page_id)
            raise
        self._tables = tables
        self._snapshot_pages = tuple(new_pages)
        for page_id in old_pages:
            try:
                self.storage.free_page(page_id)
            except StorageError:
                logger.warning("could not release old catalog page %s", page_id)
