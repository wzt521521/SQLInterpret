# 页式存储系统设计说明

负责人：wzt

## 1. 当前实现范围

`FileManager`、`BufferPool` 和对外统一门面 `StorageManager` 已接通。
数据库引擎只需导入 `minidbms.storage.StorageManager`，不依赖内部类。

模块边界保持不变：存储系统只理解逻辑页号和 `bytes`，不解析 SQL、表、列或行记录。

```text
wzy 的 Catalog / 行页格式
          ↓ 页号 + bytes
    StorageManager
          ↓
      BufferPool  ── 缓存命中、LRU/FIFO、脏页写回
          ↓
      FileManager ── 文件头、页分配位图、4096 字节页
          ↓
       数据库文件
```

`StorageManager` 实现 `common/interfaces.py` 的八个公开方法；构造参数为数据库文件路径、
缓存容量（默认 16）和替换策略（默认 LRU，大小写不敏感）。创建门面时打开文件，
关闭时先刷新所有脏页再关闭文件；重复关闭无副作用。关闭后的其他公开操作报告
`STORAGE_CLOSED`。也可使用 `with StorageManager(...) as storage:` 自动关闭。

## 2. 数据库文件格式

数据库只使用一个文件。文件由一个内部文件头和若干固定大小的数据页组成：

```text
偏移 0
┌──────────────────────┐
│ 4096 字节文件头       │
├──────────────────────┤
│ 逻辑页 0（4096 字节） │
├──────────────────────┤
│ 逻辑页 1（4096 字节） │
├──────────────────────┤
│ ...                  │
└──────────────────────┘
```

逻辑页号从 0 开始。逻辑页 `page_id` 的物理偏移为：

```text
4096 + page_id * 4096
```

文件头采用小端序，布局如下：

| 字段 | 大小 | 说明 |
| --- | ---: | --- |
| Magic | 8 字节 | 固定为 `MDBMSPG1` |
| Format version | 4 字节 | 当前为 1 |
| Page count | 8 字节 | 已创建的物理页槽数量 |
| Allocation bitmap | 4076 字节 | 每位表示对应逻辑页是否正在使用 |

位图最多描述 32608 个逻辑页。打开已有文件时会校验魔数、版本、页数量、位图范围和文件长度；不匹配时报告存储错误，不会把损坏文件当成空数据库。

空闲页状态就是分配位图中的 0 位，不另存独立空闲链表。分配或释放时先初始化/清零
数据页，再更新文件头并刷新；重启时读取位图，优先复用最小的空闲页号。文件头没有
Catalog 根页字段，因此 wzy 需要约定固定入口，例如将首次分配的逻辑页 0 保留给
Catalog，并在重启时从该页恢复。此约定需在引擎联调时确认，存储层不解释该页内容。

## 3. 页面生命周期

- `allocate_page` 优先复用最小的空闲页号；没有空闲页时在文件末尾扩展。
- 新分配页和重新分配页均初始化为全零。
- `free_page` 将页面清零并清除分配位；重复释放会报错。
- `read_page` 只读取已分配页，并始终返回 4096 字节。
- `write_page` 只接受 `bytes`；不足一页在尾部补零，超过一页直接拒绝。
- 文件头变更和页面覆盖写入会刷新到底层文件，以支持关闭后重新打开恢复。
- `close` 可重复调用；关闭后的其他操作返回 `STORAGE_CLOSED`。

## 4. Buffer Pool

缓存使用有序映射保存 `PageFrame`。每个帧包含页号、4096 字节数据、脏标记和 pin 计数。

- FIFO 按页面首次进入缓存的顺序淘汰，命中不改变顺序。
- LRU 在读命中和写命中时更新访问顺序，淘汰最久未访问页。
- 被 pin 的页不能淘汰或释放；全部帧均被 pin 时报告 `NO_EVICTABLE_PAGE`。
- 干净页淘汰时直接丢弃；脏页在淘汰前通过 `FileManager` 写回。
- `flush_page` 刷新单个脏页，`flush_all` 刷新全部脏页。
- 释放页面时丢弃对应缓存帧，避免再次命中旧内容。
- 读接口返回不可变 `bytes` 副本，调用方不能绕过写接口修改缓存。

缓存命中、未命中、淘汰和脏页写回使用 Python 标准日志模块输出。其中命中与未命中、淘汰、写回均使用 `INFO` 级别。

## 5. 统计口径

`stats()` 返回独立字典，包含：

| 键 | 含义 |
| --- | --- |
| `read_requests` | `read_page` 与 `pin_page` 的请求次数 |
| `cache_hits` | 上述读取请求在缓存中找到页面的次数 |
| `cache_misses` | 上述读取请求需要访问文件的次数 |
| `evictions` | 因容量不足被替换的页面数 |
| `dirty_writes` | 因淘汰、单页刷新或全部刷新而写回的脏页数 |

普通 `write_page` 若需要装入页面，不计入读取命中/未命中；它仍可能触发淘汰并更新淘汰与脏写统计。

## 6. 当前稳定错误码

| 错误码 | 典型触发条件 |
| --- | --- |
| `FILE_OPEN_FAILED` | 数据库路径无法创建或打开 |
| `CORRUPT_FILE` | 魔数、长度、页数量或位图损坏 |
| `UNSUPPORTED_FORMAT` | 文件版本不受支持 |
| `PAGE_LIMIT_EXCEEDED` | 位图已无法描述更多页面 |
| `INVALID_PAGE_ID` | 页号不是非负整数 |
| `PAGE_NOT_FOUND` | 页号超出已创建范围 |
| `PAGE_FREED` | 访问已经释放的页面 |
| `INVALID_DATA_TYPE` | 写入内容不是 `bytes` |
| `PAGE_DATA_TOO_LARGE` | 写入内容超过 4096 字节 |
| `INVALID_BUFFER_CAPACITY` | 缓存容量不是正整数 |
| `UNKNOWN_REPLACEMENT_POLICY` | 策略不是 LRU 或 FIFO |
| `PAGE_PINNED` | 尝试释放被 pin 的页面 |
| `PAGE_NOT_CACHED` | 尝试 unpin 非缓存页面 |
| `PAGE_NOT_PINNED` | 页面 pin 计数已经为 0 |
| `NO_EVICTABLE_PAGE` | 缓存已满且所有帧均被 pin |
| `STORAGE_CLOSED` | 文件关闭后继续操作 |
| `IO_ERROR` | 底层读取、写入、刷新或关闭失败 |

## 7. 测试

`tests/test_storage.py` 覆盖：

- 100 页连续分配、写入和重启恢复；
- 释放、最小页号复用和重分配清零；
- 0、1、4095、4096、4097 字节边界；
- 非法页号、错误数据类型、重复释放和关闭后访问；
- 损坏魔数、错误版本和截断文件；
- LRU/FIFO 的确定淘汰顺序；
- 命中、未命中、淘汰与脏写统计；
- 脏页淘汰、单页刷新、全部刷新；
- pin 防淘汰、防释放和无可淘汰页错误。
- 经 `StorageManager` 公开接口完成页面生命周期、关闭写回、重复重启、独立数据库文件、
  小容量缓存压力、损坏文件及模拟 I/O 失败后的重试。

## 8. 给数据库引擎的最小调用示例

```python
from minidbms.storage import StorageManager

with StorageManager("minidb.db", buffer_capacity=2, replacement_policy="FIFO") as storage:
    catalog_page = storage.allocate_page()  # 新文件中首次分配得到逻辑页 0
    storage.write_page(catalog_page, b"catalog bytes")
    raw = storage.read_page(catalog_page)    # 始终是 4096 字节的 bytes
    assert raw.startswith(b"catalog bytes")
    snapshot = storage.stats()               # 独立字典，可供 CLI 展示
    storage.flush_page(catalog_page)         # 也可在退出时统一 flush_all()

# with 退出时自动刷新并关闭；重新创建 StorageManager 后可读取同一页。
```

实际 Catalog 的序列化、根页约定和普通行页布局由 wzy 实现。存储层目前只验证了
两类不透明字节页经公开接口持久化；真实 Catalog 与数据页的端到端联调尚未完成。
容量为 2 的两种替换策略日志见 [storage_demo.md](storage_demo.md)。
