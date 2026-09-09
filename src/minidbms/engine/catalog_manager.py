"""Persistent catalog skeleton. Implementation owner: wzy."""

from minidbms.common.types import TableSchema


class CatalogManager:
    def table_exists(self, table_name: str) -> bool:
        raise NotImplementedError("wzy: implement catalog lookup")

    def get_schema(self, table_name: str) -> TableSchema:
        raise NotImplementedError("wzy: implement schema lookup")

    def create_table(self, schema: TableSchema) -> None:
        raise NotImplementedError("wzy: implement persistent catalog updates")
