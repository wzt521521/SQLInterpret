"""Fixed-size disk page manager skeleton. Implementation owner: wzt."""

from pathlib import Path


class FileManager:
    def __init__(self, db_path: str | Path) -> None:
        self.db_path = Path(db_path)

    def allocate_page(self) -> int:
        raise NotImplementedError("wzt: implement persistent page allocation")

    def free_page(self, page_id: int) -> None:
        raise NotImplementedError("wzt: implement page release")

    def read_page(self, page_id: int) -> bytes:
        raise NotImplementedError("wzt: implement fixed-size page reads")

    def write_page(self, page_id: int, data: bytes) -> None:
        raise NotImplementedError("wzt: implement fixed-size page writes")

    def close(self) -> None:
        raise NotImplementedError("wzt: implement safe file close")
