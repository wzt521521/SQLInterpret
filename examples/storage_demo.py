"""Show LRU/FIFO eviction, dirty writes and restart using temporary files."""

from __future__ import annotations

import logging
from pathlib import Path
import sys
from tempfile import TemporaryDirectory

from minidbms.storage import StorageManager


def demonstrate(policy: str) -> None:
    with TemporaryDirectory(prefix="minidb-storage-demo-") as directory:
        db_path = Path(directory) / "demo.db"
        print(f"\n{policy} (capacity=2)")
        with StorageManager(db_path, buffer_capacity=2, replacement_policy=policy) as storage:
            pages = [storage.allocate_page() for _ in range(3)]
            storage.read_page(pages[0])
            storage.read_page(pages[1])
            storage.read_page(pages[0])
            storage.write_page(pages[0], b"updated page 0")
            storage.read_page(pages[2])
            print(f"stats before close: {storage.stats()}")
        with StorageManager(db_path, buffer_capacity=2, replacement_policy=policy) as reopened:
            print(f"page 0 after reopen: {reopened.read_page(0).rstrip(bytes(1))!r}")


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s", stream=sys.stdout)
    demonstrate("LRU")
    demonstrate("FIFO")
