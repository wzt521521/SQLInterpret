# wzt 模块清单大纲

> 项目：MiniDBMS / SQLInterpret<br>
> 负责人：wzt<br>
> 负责模块：页式存储与缓存系统<br>
> 核心目标：向数据库引擎提供稳定的“页号 + bytes”持久化接口。

## 一、主要职责

wzt 主要负责 MiniDBMS 的底层存储系统，包括数据库文件管理、固定大小页面管理、Buffer Pool、缓存替换、脏页写回和存储统计。该模块承接上层数据库引擎的数据读写请求，并将页面内容可靠地保存到磁盘文件。

## 二、模块组成

### 1. 页面基础结构

- 固定页面大小 `PAGE_SIZE = 4096`。
- 定义页号、页面帧、脏页状态和 pin 计数等基础结构。
- 统一存储异常及错误码。

### 2. FileManager：磁盘文件与页面管理

- 创建、打开和校验数据库文件。
- 管理文件头、格式版本、页面数量和分配位图。
- 分配、释放和复用逻辑页面。
- 完成逻辑页号与磁盘偏移的转换。
- 读取和覆盖写入固定大小页面。
- 持久化页面分配状态并识别损坏文件。

### 3. BufferPool：页面缓存

- 根据配置容量缓存磁盘页面。
- 处理缓存命中和未命中。
- 支持 LRU、FIFO 两种替换策略。
- 管理干净页、脏页以及 pin/unpin 状态。
- 淘汰脏页前写回磁盘。
- 支持单页刷新和全部刷新。
- 记录读取、命中、未命中、淘汰和脏写统计。

### 4. StorageManager：统一存储接口

- 连接 `FileManager` 与 `BufferPool`。
- 对上层提供统一的页面操作入口。
- 负责参数校验、错误转换、安全关闭和资源释放。
- 对外提供：
  - `allocate_page()`
  - `free_page(page_id)`
  - `read_page(page_id)`
  - `write_page(page_id, data)`
  - `flush_page(page_id)`
  - `flush_all()`
  - `stats()`
  - `close()`

## 三、与其他成员的接口边界

- 接收和返回的核心对象只有页号与 `bytes`。
- 不解析 SQL、Token、AST 或执行计划。
- 不理解表名、列名、Schema 和行记录含义。
- 不实现 Catalog、行序列化、WHERE 求值或 CLI。
- wzy 的数据库引擎必须通过 `StorageManager` 访问页面，不能直接操作底层文件或缓存内部结构。

## 四、主要交付文件

- `src/minidbms/storage/page.py`
- `src/minidbms/storage/errors.py`
- `src/minidbms/storage/file_manager.py`
- `src/minidbms/storage/buffer.py`
- `src/minidbms/storage/storage_manager.py`
- `src/minidbms/storage/__init__.py`
- `tests/test_storage.py`
- `examples/storage_demo.py`
- `docs/storage_design.md`
- `docs/storage_demo.md`

## 五、重点验收内容

- 页面能够分配、释放、安全复用、读取和写入。
- 页面大小固定为 4096 字节，短数据补零，超长数据拒绝。
- 数据和页面分配状态在程序重启后仍然正确。
- LRU 与 FIFO 能产生符合规则且不同的淘汰结果。
- 脏页在刷新、淘汰和关闭前可靠写回。
- 被 pin 的页面不会被错误淘汰或释放。
- 缓存统计和运行日志正确、可展示。
- 非法页号、已释放页面、损坏文件和 I/O 失败能够返回清晰错误。
- 数据库引擎能够只通过 `StorageManager` 完成 Catalog 和数据页持久化。

## 六、测试与演示

- 使用 `tests/test_storage.py` 验证页面生命周期、持久化、异常处理、LRU/FIFO、脏页写回和统计。
- 使用 `examples/storage_demo.py` 演示缓存命中、淘汰顺序、脏页刷新和重启读取。
- 参加全仓端到端测试，确认存储模块能够支撑 SQL 编译器和数据库引擎的完整链路。

## 七、不属于 wzt 的工作

- SQL 词法、语法、语义分析和执行计划生成。
- Catalog、Schema、行编码、槽页记录和删除标记的业务解释。
- CREATE、INSERT、SELECT、DELETE 执行器和表达式求值。
- 命令行界面和 SQL 输出格式。
- 索引、事务、WAL、并发控制、MVCC 和崩溃恢复。
