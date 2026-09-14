"""Acceptance tests for wzt's disk page manager and buffer pool."""

import os
from pathlib import Path

import pytest

from minidbms.storage.buffer import BufferPool, ReplacementPolicy
from minidbms.storage.errors import StorageError
from minidbms.storage.file_manager import FileManager
from minidbms.storage.page import PAGE_SIZE
from minidbms.storage import StorageManager
from minidbms.common.errors import ErrorStage
from minidbms.common.interfaces import StorageManagerProtocol


def assert_storage_error(code: str, operation) -> None:
    with pytest.raises(StorageError) as caught:
        operation()
    assert caught.value.code == code


def allocate_pages(manager: FileManager, count: int) -> list[int]:
    return [manager.allocate_page() for _ in range(count)]


class TestFileManager:
    def test_allocation_state_survives_many_pages(self, tmp_path: Path) -> None:
        db_path = tmp_path / "many-pages.db"
        with FileManager(db_path) as manager:
            assert allocate_pages(manager, 100) == list(range(100))
            manager.write_page(0, b"first")
            manager.write_page(99, b"last")

        with FileManager(db_path) as reopened:
            assert reopened.page_count == 100
            assert all(reopened.is_allocated(page_id) for page_id in range(100))
            assert reopened.read_page(0).startswith(b"first")
            assert reopened.read_page(99).startswith(b"last")

    def test_allocate_write_read_free_reuse_and_restart(self, tmp_path: Path) -> None:
        db_path = tmp_path / "pages.db"
        manager = FileManager(db_path)
        assert allocate_pages(manager, 3) == [0, 1, 2]
        manager.write_page(0, b"alpha")
        manager.write_page(1, b"beta")
        manager.free_page(1)
        manager.close()

        reopened = FileManager(db_path)
        assert reopened.page_count == 3
        assert reopened.read_page(0) == b"alpha" + bytes(PAGE_SIZE - 5)
        assert_storage_error("PAGE_FREED", lambda: reopened.read_page(1))
        assert reopened.allocate_page() == 1
        assert reopened.read_page(1) == bytes(PAGE_SIZE)
        assert reopened.allocate_page() == 3
        reopened.close()

        final = FileManager(db_path)
        assert final.page_count == 4
        assert final.read_page(0).startswith(b"alpha")
        assert final.read_page(1) == bytes(PAGE_SIZE)
        final.close()

    @pytest.mark.parametrize("size", [0, 1, PAGE_SIZE - 1, PAGE_SIZE])
    def test_write_sizes_are_padded_to_one_page(self, tmp_path: Path, size: int) -> None:
        with FileManager(tmp_path / f"size-{size}.db") as manager:
            page_id = manager.allocate_page()
            data = bytes((index % 251 for index in range(size)))
            manager.write_page(page_id, data)
            actual = manager.read_page(page_id)
            assert len(actual) == PAGE_SIZE
            assert actual == data + bytes(PAGE_SIZE - size)

    def test_invalid_page_operations_and_data_are_rejected(self, tmp_path: Path) -> None:
        manager = FileManager(tmp_path / "errors.db")
        page_id = manager.allocate_page()
        assert_storage_error("INVALID_PAGE_ID", lambda: manager.read_page(-1))
        assert_storage_error("INVALID_PAGE_ID", lambda: manager.read_page(True))
        assert_storage_error("PAGE_NOT_FOUND", lambda: manager.read_page(99))
        assert_storage_error(
            "PAGE_DATA_TOO_LARGE",
            lambda: manager.write_page(page_id, bytes(PAGE_SIZE + 1)),
        )
        assert manager.read_page(page_id) == bytes(PAGE_SIZE)
        assert_storage_error(
            "INVALID_DATA_TYPE", lambda: manager.write_page(page_id, bytearray(1))
        )
        manager.free_page(page_id)
        assert_storage_error("PAGE_FREED", lambda: manager.free_page(page_id))
        assert_storage_error("PAGE_FREED", lambda: manager.write_page(page_id, b"x"))
        manager.close()
        manager.close()
        assert_storage_error("STORAGE_CLOSED", lambda: manager.read_page(page_id))

    @pytest.mark.parametrize(
        ("contents", "code"),
        [
            (b"too short", "CORRUPT_FILE"),
            (bytes(PAGE_SIZE), "CORRUPT_FILE"),
        ],
    )
    def test_corrupt_file_is_rejected(
        self, tmp_path: Path, contents: bytes, code: str
    ) -> None:
        db_path = tmp_path / "corrupt.db"
        db_path.write_bytes(contents)
        assert_storage_error(code, lambda: FileManager(db_path))

    def test_truncated_data_page_is_rejected_on_open(self, tmp_path: Path) -> None:
        db_path = tmp_path / "truncated.db"
        with FileManager(db_path) as manager:
            manager.allocate_page()
        with db_path.open("r+b") as file:
            file.truncate(PAGE_SIZE + 10)
        assert_storage_error("CORRUPT_FILE", lambda: FileManager(db_path))

    def test_unsupported_file_format_is_rejected(self, tmp_path: Path) -> None:
        db_path = tmp_path / "future.db"
        with FileManager(db_path):
            pass
        with db_path.open("r+b") as file:
            file.seek(8)
            file.write((FileManager.FORMAT_VERSION + 1).to_bytes(4, "little"))
        assert_storage_error("UNSUPPORTED_FORMAT", lambda: FileManager(db_path))


class TestBufferPool:
    def create_file_manager(self, tmp_path: Path, count: int = 3) -> FileManager:
        manager = FileManager(tmp_path / "buffer.db")
        for page_id in allocate_pages(manager, count):
            manager.write_page(page_id, f"page-{page_id}".encode())
        return manager

    def test_lru_replacement_order_and_statistics(self, tmp_path: Path) -> None:
        manager = self.create_file_manager(tmp_path)
        pool = BufferPool(2, ReplacementPolicy.LRU, manager)
        pool.read_page(0)
        pool.read_page(1)
        pool.read_page(0)
        pool.read_page(2)

        assert pool.resident_page_ids() == (0, 2)
        assert pool.stats() == {
            "read_requests": 4,
            "cache_hits": 1,
            "cache_misses": 3,
            "evictions": 1,
            "dirty_writes": 0,
        }
        copied_stats = pool.stats()
        copied_stats["cache_hits"] = 999
        assert pool.stats()["cache_hits"] == 1
        manager.close()

    def test_fifo_hit_does_not_change_insertion_order(self, tmp_path: Path) -> None:
        manager = self.create_file_manager(tmp_path)
        pool = BufferPool(2, "fifo", manager)
        pool.read_page(0)
        pool.read_page(1)
        pool.read_page(0)
        pool.read_page(2)

        assert pool.policy == ReplacementPolicy.FIFO
        assert pool.resident_page_ids() == (1, 2)
        assert pool.stats()["evictions"] == 1
        manager.close()

    def test_lru_write_hit_updates_recently_used_order(self, tmp_path: Path) -> None:
        manager = self.create_file_manager(tmp_path)
        pool = BufferPool(2, "LRU", manager)
        pool.read_page(0)
        pool.read_page(1)
        pool.write_page(0, b"new zero")
        pool.read_page(2)

        assert pool.resident_page_ids() == (0, 2)
        assert pool.stats()["evictions"] == 1
        manager.close()

    def test_dirty_page_is_written_before_eviction(self, tmp_path: Path) -> None:
        manager = self.create_file_manager(tmp_path, count=2)
        pool = BufferPool(1, "LRU", manager)
        pool.write_page(0, b"changed")
        assert manager.read_page(0).startswith(b"page-0")

        pool.read_page(1)

        assert manager.read_page(0) == b"changed" + bytes(PAGE_SIZE - 7)
        assert pool.stats()["evictions"] == 1
        assert pool.stats()["dirty_writes"] == 1
        manager.close()
        with FileManager(tmp_path / "buffer.db") as reopened:
            assert reopened.read_page(0) == b"changed" + bytes(PAGE_SIZE - 7)

    def test_flush_page_and_flush_all_only_write_dirty_frames(self, tmp_path: Path) -> None:
        manager = self.create_file_manager(tmp_path)
        pool = BufferPool(3, "LRU", manager)
        pool.read_page(0)
        pool.write_page(1, b"one")
        pool.write_page(2, b"two")

        pool.flush_page(1)
        assert pool.stats()["dirty_writes"] == 1
        assert manager.read_page(1).startswith(b"one")
        pool.flush_all()
        assert pool.stats()["dirty_writes"] == 2
        assert manager.read_page(2).startswith(b"two")
        pool.flush_all()
        assert pool.stats()["dirty_writes"] == 2
        manager.close()

    def test_pinned_page_cannot_be_evicted_or_freed(self, tmp_path: Path) -> None:
        manager = self.create_file_manager(tmp_path, count=2)
        pool = BufferPool(1, "LRU", manager)
        pool.pin_page(0)

        assert_storage_error("NO_EVICTABLE_PAGE", lambda: pool.read_page(1))
        assert pool.resident_page_ids() == (0,)
        assert_storage_error("PAGE_PINNED", lambda: pool.free_page(0))
        pool.unpin_page(0)
        assert_storage_error("PAGE_NOT_PINNED", lambda: pool.unpin_page(0))
        pool.read_page(1)
        assert pool.resident_page_ids() == (1,)
        manager.close()

    def test_free_removes_cached_copy_and_reused_page_is_zeroed(self, tmp_path: Path) -> None:
        manager = self.create_file_manager(tmp_path, count=1)
        pool = BufferPool(1, "LRU", manager)
        pool.write_page(0, b"dirty old value")
        pool.free_page(0)
        assert pool.resident_page_ids() == ()
        assert manager.allocate_page() == 0
        assert pool.read_page(0) == bytes(PAGE_SIZE)
        manager.close()

    @pytest.mark.parametrize(
        ("capacity", "policy", "code"),
        [
            (0, "LRU", "INVALID_BUFFER_CAPACITY"),
            (-1, "LRU", "INVALID_BUFFER_CAPACITY"),
            (True, "LRU", "INVALID_BUFFER_CAPACITY"),
            (1, "CLOCK", "UNKNOWN_REPLACEMENT_POLICY"),
        ],
    )
    def test_invalid_configuration(
        self, tmp_path: Path, capacity: int, policy: str, code: str
    ) -> None:
        manager = self.create_file_manager(tmp_path, count=0)
        assert_storage_error(code, lambda: BufferPool(capacity, policy, manager))
        manager.close()


class TestStorageManager:
    def test_public_protocol_page_lifecycle_and_restart(self, tmp_path: Path) -> None:
        db_path = tmp_path / "public.db"
        with StorageManager(db_path, buffer_capacity=2, replacement_policy="lru") as storage:
            consumer: StorageManagerProtocol = storage
            assert consumer.PAGE_SIZE == PAGE_SIZE
            assert [consumer.allocate_page() for _ in range(3)] == [0, 1, 2]
            consumer.write_page(0, b"catalog")
            consumer.write_page(2, b"row-data")
            assert type(consumer.read_page(0)) is bytes
            assert consumer.read_page(0) == b"catalog" + bytes(PAGE_SIZE - 7)
            consumer.free_page(1)

        with StorageManager(db_path, buffer_capacity=1, replacement_policy="FIFO") as reopened:
            assert reopened.read_page(0).startswith(b"catalog")
            assert_storage_error("PAGE_FREED", lambda: reopened.read_page(1))
            assert reopened.read_page(2).startswith(b"row-data")
            assert reopened.allocate_page() == 1
            assert reopened.read_page(1) == bytes(PAGE_SIZE)

    @pytest.mark.parametrize("size", [0, 1, PAGE_SIZE - 1, PAGE_SIZE])
    def test_public_write_boundaries(self, tmp_path: Path, size: int) -> None:
        with StorageManager(tmp_path / f"size-{size}.db") as storage:
            page_id = storage.allocate_page()
            data = b"x" * size
            storage.write_page(page_id, data)
            assert storage.read_page(page_id) == data + bytes(PAGE_SIZE - size)
            assert_storage_error(
                "PAGE_DATA_TOO_LARGE",
                lambda: storage.write_page(page_id, b"y" * (PAGE_SIZE + 1)),
            )
            assert storage.read_page(page_id) == data + bytes(PAGE_SIZE - size)

    def test_public_errors_and_closed_state(self, tmp_path: Path) -> None:
        storage = StorageManager(tmp_path / "errors.db")
        page_id = storage.allocate_page()
        with pytest.raises(StorageError) as caught:
            storage.read_page(-1)
        assert caught.value.stage == ErrorStage.STORAGE
        assert caught.value.code == "INVALID_PAGE_ID"
        assert_storage_error("PAGE_NOT_FOUND", lambda: storage.read_page(5))
        assert_storage_error("INVALID_DATA_TYPE", lambda: storage.write_page(page_id, bytearray(1)))
        storage.free_page(page_id)
        assert_storage_error("PAGE_FREED", lambda: storage.read_page(page_id))
        assert_storage_error("PAGE_FREED", lambda: storage.free_page(page_id))
        storage.close()
        storage.close()
        for operation in (
            storage.allocate_page,
            lambda: storage.free_page(page_id),
            lambda: storage.read_page(page_id),
            lambda: storage.write_page(page_id, b"x"),
            lambda: storage.flush_page(page_id),
            storage.flush_all,
            storage.stats,
        ):
            assert_storage_error("STORAGE_CLOSED", operation)

    @pytest.mark.parametrize("policy,expected_hit", [("lRu", 1), ("fIfO", 0)])
    def test_public_replacement_and_statistics(
        self, tmp_path: Path, policy: str, expected_hit: int, caplog
    ) -> None:
        with StorageManager(tmp_path / f"{policy}.db", 2, policy) as storage:
            pages = [storage.allocate_page() for _ in range(3)]
            with caplog.at_level("INFO", logger="minidbms.storage.buffer"):
                for page_id in (pages[0], pages[1], pages[0], pages[2], pages[0]):
                    storage.read_page(page_id)
            stats = storage.stats()
            assert stats["read_requests"] == 5
            assert stats["cache_hits"] == 1 + expected_hit
            assert stats["cache_misses"] == 4 - expected_hit
            assert stats["evictions"] == 2 - expected_hit
            assert stats["dirty_writes"] == 0
            stats["cache_hits"] = 999
            assert storage.stats()["cache_hits"] == 1 + expected_hit
            assert "buffer evict" in caplog.text

    def test_public_flush_page_flush_all_and_close(self, tmp_path: Path) -> None:
        db_path = tmp_path / "flush.db"
        storage = StorageManager(db_path, buffer_capacity=2)
        first, second = storage.allocate_page(), storage.allocate_page()
        storage.write_page(first, b"first")
        storage.write_page(second, b"second")
        storage.flush_page(first)
        with FileManager(db_path) as disk:
            assert disk.read_page(first).startswith(b"first")
            assert disk.read_page(second) == bytes(PAGE_SIZE)
        assert storage.stats()["dirty_writes"] == 1
        storage.flush_all()
        assert storage.stats()["dirty_writes"] == 2
        storage.write_page(first, b"after flush")
        storage.close()
        with StorageManager(db_path) as reopened:
            assert reopened.read_page(first).startswith(b"after flush")
            assert reopened.read_page(second).startswith(b"second")

    def test_free_discards_dirty_cache_and_reuse_is_zeroed(self, tmp_path: Path) -> None:
        with StorageManager(tmp_path / "reuse.db", 1) as storage:
            page_id = storage.allocate_page()
            storage.write_page(page_id, b"old dirty content")
            storage.free_page(page_id)
            assert_storage_error("PAGE_FREED", lambda: storage.read_page(page_id))
            assert storage.allocate_page() == page_id
            assert storage.read_page(page_id) == bytes(PAGE_SIZE)

    def test_independent_files_and_multiple_restarts(self, tmp_path: Path) -> None:
        left_path, right_path = tmp_path / "left.db", tmp_path / "right.db"
        with StorageManager(left_path, 1) as left, StorageManager(right_path, 1) as right:
            assert left.allocate_page() == right.allocate_page() == 0
            left.write_page(0, b"left")
            right.write_page(0, b"right")
        for _ in range(3):
            with StorageManager(left_path, 1) as left, StorageManager(right_path, 1) as right:
                assert left.read_page(0).startswith(b"left")
                assert right.read_page(0).startswith(b"right")
                left.write_page(0, b"left")

    def test_small_cache_pressure_preserves_pages(self, tmp_path: Path) -> None:
        db_path = tmp_path / "pressure.db"
        with StorageManager(db_path, buffer_capacity=2, replacement_policy="FIFO") as storage:
            for page_id in range(30):
                assert storage.allocate_page() == page_id
                storage.write_page(page_id, f"value-{page_id}".encode())
            for page_id in reversed(range(30)):
                assert storage.read_page(page_id).startswith(f"value-{page_id}".encode())
            assert storage.stats()["evictions"] > 0
            assert storage.stats()["dirty_writes"] > 0
        with StorageManager(db_path, buffer_capacity=2) as reopened:
            for page_id in range(30):
                assert reopened.read_page(page_id).startswith(f"value-{page_id}".encode())

    def test_invalid_configuration_and_corrupt_file(self, tmp_path: Path) -> None:
        assert_storage_error(
            "INVALID_BUFFER_CAPACITY", lambda: StorageManager(tmp_path / "bad-cap.db", 0)
        )
        assert_storage_error(
            "UNKNOWN_REPLACEMENT_POLICY",
            lambda: StorageManager(tmp_path / "bad-policy.db", 1, "CLOCK"),
        )
        corrupt = tmp_path / "corrupt.db"
        corrupt.write_bytes(b"invalid header")
        assert_storage_error("CORRUPT_FILE", lambda: StorageManager(corrupt))

    def test_io_failure_is_storage_error_and_flush_can_retry(
        self, tmp_path: Path, monkeypatch
    ) -> None:
        db_path = tmp_path / "io.db"
        storage = StorageManager(db_path)
        page_id = storage.allocate_page()
        storage.write_page(page_id, b"retry me")

        def fail_fsync(_descriptor: int) -> None:
            raise OSError("injected fsync failure")

        with monkeypatch.context() as patch:
            patch.setattr(os, "fsync", fail_fsync)
            assert_storage_error("IO_ERROR", storage.flush_all)
        storage.close()
        with StorageManager(db_path) as reopened:
            assert reopened.read_page(page_id).startswith(b"retry me")
