"""Fixed-capacity page cache with LRU and FIFO replacement."""

from __future__ import annotations

from collections import OrderedDict
from enum import Enum
import logging
from threading import RLock

from .errors import StorageError
from .file_manager import FileManager
from .page import PageFrame


logger = logging.getLogger(__name__)


class ReplacementPolicy(str, Enum):
    LRU = "LRU"
    FIFO = "FIFO"


class BufferPool:
    """Cache pages and write dirty frames back before eviction."""

    def __init__(
        self,
        capacity: int,
        policy: ReplacementPolicy | str,
        file_manager: FileManager,
    ) -> None:
        if isinstance(capacity, bool) or not isinstance(capacity, int) or capacity <= 0:
            raise StorageError("INVALID_BUFFER_CAPACITY", "buffer capacity must be positive")
        try:
            normalized_policy = (
                policy.value
                if isinstance(policy, ReplacementPolicy)
                else str(policy).upper()
            )
            self.policy = ReplacementPolicy(normalized_policy)
        except ValueError as error:
            raise StorageError(
                "UNKNOWN_REPLACEMENT_POLICY",
                f"replacement policy must be LRU or FIFO, got {policy!r}",
            ) from error

        self.capacity = capacity
        self.file_manager = file_manager
        self._frames: OrderedDict[int, PageFrame] = OrderedDict()
        self._lock = RLock()
        self._stats = {
            "read_requests": 0,
            "cache_hits": 0,
            "cache_misses": 0,
            "evictions": 0,
            "dirty_writes": 0,
        }

    def read_page(self, page_id: int) -> bytes:
        """Read a page through the cache and return an immutable copy."""
        with self._lock:
            self._stats["read_requests"] += 1
            frame = self._get_frame(page_id, record_cache_result=True)
            return bytes(frame.data)

    def write_page(self, page_id: int, data: bytes) -> None:
        """Update a cached page and mark it dirty without immediate disk I/O."""
        normalized = FileManager.normalize_page_data(data)
        with self._lock:
            frame = self._get_frame(page_id, record_cache_result=False)
            frame.data[:] = normalized
            frame.dirty = True
            self._touch(page_id)
            logger.debug("buffer write page=%s dirty=true", page_id)

    def pin_page(self, page_id: int) -> bytes:
        """Pin a page against eviction and return an immutable data copy."""
        with self._lock:
            self._stats["read_requests"] += 1
            frame = self._get_frame(page_id, record_cache_result=True)
            frame.pin_count += 1
            logger.debug("buffer pin page=%s count=%s", page_id, frame.pin_count)
            return bytes(frame.data)

    def unpin_page(self, page_id: int) -> None:
        with self._lock:
            frame = self._frames.get(page_id)
            if frame is None:
                raise StorageError("PAGE_NOT_CACHED", f"page {page_id} is not cached")
            if frame.pin_count == 0:
                raise StorageError("PAGE_NOT_PINNED", f"page {page_id} is not pinned")
            frame.pin_count -= 1
            logger.debug("buffer unpin page=%s count=%s", page_id, frame.pin_count)

    def free_page(self, page_id: int) -> None:
        """Release a page and remove any unpinned cached copy."""
        with self._lock:
            frame = self._frames.get(page_id)
            if frame is not None and frame.pin_count:
                raise StorageError(
                    "PAGE_PINNED", f"cannot free pinned page {page_id}"
                )
            self.file_manager.free_page(page_id)
            self._frames.pop(page_id, None)
            logger.debug("buffer discard freed page=%s", page_id)

    def flush_page(self, page_id: int) -> None:
        """Write one dirty cached page to disk."""
        with self._lock:
            frame = self._frames.get(page_id)
            if frame is None:
                # Validate the id. An allocated non-resident page is already clean.
                self.file_manager.read_page(page_id)
                return
            self._flush_frame(frame)

    def flush_all(self) -> None:
        """Write all dirty cached pages to disk."""
        with self._lock:
            for frame in self._frames.values():
                self._flush_frame(frame)

    def stats(self) -> dict[str, int]:
        """Return a copy so callers cannot mutate internal counters."""
        with self._lock:
            return dict(self._stats)

    def resident_page_ids(self) -> tuple[int, ...]:
        """Expose resident ids in replacement order for diagnostics/tests."""
        with self._lock:
            return tuple(self._frames)

    def _get_frame(self, page_id: int, *, record_cache_result: bool) -> PageFrame:
        frame = self._frames.get(page_id)
        if frame is not None:
            if record_cache_result:
                self._stats["cache_hits"] += 1
            self._touch(page_id)
            logger.info("buffer hit page=%s", page_id)
            return frame

        if record_cache_result:
            self._stats["cache_misses"] += 1
        logger.info("buffer miss page=%s", page_id)

        # Read before evicting so an invalid id cannot disturb a valid cache.
        page_data = self.file_manager.read_page(page_id)
        if len(self._frames) >= self.capacity:
            self._evict_one()
        frame = PageFrame(page_id=page_id, data=bytearray(page_data))
        self._frames[page_id] = frame
        return frame

    def _touch(self, page_id: int) -> None:
        if self.policy == ReplacementPolicy.LRU:
            self._frames.move_to_end(page_id)

    def _evict_one(self) -> None:
        victim_id = next(
            (
                page_id
                for page_id, frame in self._frames.items()
                if frame.pin_count == 0
            ),
            None,
        )
        if victim_id is None:
            raise StorageError(
                "NO_EVICTABLE_PAGE", "all cached pages are pinned"
            )
        victim = self._frames[victim_id]
        was_dirty = victim.dirty
        self._flush_frame(victim)
        del self._frames[victim_id]
        self._stats["evictions"] += 1
        logger.info(
            "buffer evict page=%s policy=%s dirty=%s",
            victim_id,
            self.policy.value,
            was_dirty,
        )

    def _flush_frame(self, frame: PageFrame) -> None:
        if not frame.dirty:
            return
        self.file_manager.write_page(frame.page_id, bytes(frame.data))
        frame.dirty = False
        self._stats["dirty_writes"] += 1
        logger.info("buffer flush dirty page=%s", frame.page_id)
