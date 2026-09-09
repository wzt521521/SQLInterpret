"""Page constants and in-memory frame metadata. Implementation owner: wzt."""

from dataclasses import dataclass

PAGE_SIZE = 4096


@dataclass(slots=True)
class PageFrame:
    page_id: int
    data: bytearray
    dirty: bool = False
    pin_count: int = 0
