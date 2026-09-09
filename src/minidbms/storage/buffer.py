"""Buffer pool skeleton with selectable replacement policy. Implementation owner: wzt."""

from enum import Enum


class ReplacementPolicy(str, Enum):
    LRU = "LRU"
    FIFO = "FIFO"


class BufferPool:
    def __init__(self, capacity: int, policy: ReplacementPolicy) -> None:
        self.capacity = capacity
        self.policy = policy

    def flush_all(self) -> None:
        raise NotImplementedError("wzt: implement dirty-page flushing")
