"""Public storage facade skeleton. Implementation owner: wzt."""

from pathlib import Path

from .page import PAGE_SIZE


class StorageManager:
    PAGE_SIZE = PAGE_SIZE

    def __init__(
        self,
        db_path: str | Path,
        buffer_capacity: int = 16,
        replacement_policy: str = "LRU",
    ) -> None:
        self.db_path = Path(db_path)
        self.buffer_capacity = buffer_capacity
        self.replacement_policy = replacement_policy.upper()

    def allocate_page(self) -> int:
        raise NotImplementedError("wzt: implement page allocation")

    def free_page(self, page_id: int) -> None:
        raise NotImplementedError("wzt: implement page release")

    def read_page(self, page_id: int) -> bytes:
        raise NotImplementedError("wzt: implement buffered page reads")

    def write_page(self, page_id: int, data: bytes) -> None:
        raise NotImplementedError("wzt: implement buffered page writes")

    def flush_page(self, page_id: int) -> None:
        raise NotImplementedError("wzt: implement single-page flush")

    def flush_all(self) -> None:
        raise NotImplementedError("wzt: implement full buffer flush")

    def stats(self) -> dict[str, int]:
        raise NotImplementedError("wzt: implement buffer statistics")

    def close(self) -> None:
        raise NotImplementedError("wzt: implement safe storage close")
