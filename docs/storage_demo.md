# 容量为 2 的 Buffer Pool 演示记录

运行 `python examples/storage_demo.py`。脚本只使用系统临时目录，并在退出时删除数据库文件。
两次演示均按 `读 0 → 读 1 → 读 0 → 写 0 → 读 2` 访问页面。

```text
LRU (capacity=2)
buffer miss page=0
buffer miss page=1
buffer hit page=0
buffer hit page=0
buffer miss page=2
buffer evict page=1 policy=LRU dirty=False
stats before close: {'read_requests': 4, 'cache_hits': 1, 'cache_misses': 3, 'evictions': 1, 'dirty_writes': 0}
buffer flush dirty page=0
buffer miss page=0
page 0 after reopen: b'updated page 0'

FIFO (capacity=2)
buffer miss page=0
buffer miss page=1
buffer hit page=0
buffer hit page=0
buffer miss page=2
buffer flush dirty page=0
buffer evict page=0 policy=FIFO dirty=True
stats before close: {'read_requests': 4, 'cache_hits': 1, 'cache_misses': 3, 'evictions': 1, 'dirty_writes': 1}
buffer miss page=0
page 0 after reopen: b'updated page 0'
```

LRU 淘汰页 1；FIFO 淘汰页 0，先将其脏数据写回。两者重新打开文件后均读到
`updated page 0`。日志中的第二次 `buffer hit page=0` 来自写请求；按当前统计口径，
`read_requests` 和 `cache_hits` 只统计读请求，故输出分别为 4 和 1。
