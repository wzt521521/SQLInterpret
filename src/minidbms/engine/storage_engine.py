"""Row-to-page storage bridge skeleton. Implementation owner: wzy."""

from minidbms.common.interfaces import StorageManagerProtocol


class StorageEngine:
    def __init__(self, storage: StorageManagerProtocol) -> None:
        self.storage = storage
