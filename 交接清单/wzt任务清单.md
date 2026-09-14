# wzt 页式存储与缓存系统任务清单

> 项目：MiniDBMS / SQLInterpret  
> 负责人：wzt  
> 整理日期：2026-09-10；进度复核：2026-09-14  
> 工作分支：`codex/storage-wzt-stage1-2`  
> 目标：实现稳定的 `StorageManager`，只处理“页号 + bytes”，并交付给 wzy 的数据库引擎使用。

## 一、当前进度

以下为 2026-09-14 的实测进度；阶段 1、2 从 2026-09-10 的本地改动继续完成。

- [x] 已建立 `storage/` 模块目录和基础类名。
- [x] 已固定 `PAGE_SIZE = 4096`。
- [x] 已定义 `PageFrame`、`StorageError`、`ReplacementPolicy` 和完整 `StorageManager`。
- [x] 公共 `StorageManagerProtocol` 已定义以下方法：`allocate_page`、`free_page`、`read_page`、`write_page`、`flush_page`、`flush_all`、`stats`、`close`。
- [x] `FileManager` 的页分配、释放、读写和持久化已经实现。
- [x] `BufferPool` 的缓存、LRU/FIFO、脏页回写和统计已经实现。
- [x] `StorageManager` 的八个公开方法已接通 `FileManager` 与 `BufferPool`，不再抛出 `NotImplementedError`。
- [x] `tests/test_storage.py` 已有 36 项功能测试，全部通过。
- [x] `docs/storage_design.md` 已记录文件格式、页面生命周期、缓存策略、统计口径、错误码和 wzy 调用示例。

存储模块独立验收时全仓 100 项通过，其中 36 项为存储测试；提交 `7276ec2` 已通过 [PR #2](https://github.com/wzt521521/SQLInterpret/pull/2) 合入 `main`。后续 wzy 引擎在 [PR #3](https://github.com/wzt521521/SQLInterpret/pull/3) 中完成真实 Catalog 与数据页联调，目前全仓 118 项测试通过。

---

## 二、开工前先冻结的设计

- [x] 已从最新 `main` 创建本地 `codex/storage-wzt-stage1-2`，代码已提交并推送远端。
- [x] 确认页号从 `0` 开始，并写入设计文档和测试。
- [x] wzy 引擎已采用逻辑页 0 作为 Catalog 固定入口；`docs/api_contract.md` 与跨进程重启测试已验证此约定（见 PR #3）。
- [x] 确认数据库文件布局：使用一个 4096 字节文件头保存魔数、格式版本、页数量和分配位图；逻辑数据页从文件头之后开始，文件头不暴露为普通数据页。
- [x] 固定文件魔数 `MDBMSPG1` 和格式版本 1；文件格式不匹配时必须报错，不能按空数据库继续运行。
- [x] 固定空闲页复用规则：优先复用最小页号，并已编写测试。
- [x] 固定 `stats()` 的键名：
  - `read_requests`
  - `cache_hits`
  - `cache_misses`
  - `evictions`
  - `dirty_writes`
- [x] 固定异常码，并在 `docs/storage_design.md` 中记录非法页号、页面不存在、页面已释放、页面数据过长、参数类型错误、未知替换策略、无可淘汰页、文件损坏、I/O 失败、对象已关闭等错误。
- [x] 未修改 `src/minidbms/common/interfaces.py` 的公共协议。

完成标志：wzy 能根据文档明确知道页号范围、写入补齐规则、统计字段、错误码和关闭语义。

---

## 三、阶段 1：实现磁盘文件与页管理

主要文件：

- `src/minidbms/storage/file_manager.py`
- `src/minidbms/storage/page.py`
- `src/minidbms/storage/errors.py`

任务：

- [x] 新数据库文件不存在时，创建文件并写入合法文件头。
- [x] 打开已有数据库时，读取并校验魔数、版本、文件长度和页分配元数据。
- [x] 实现逻辑页号到物理文件偏移的换算，确保文件头不会被普通页读写覆盖。
- [x] 实现 `allocate_page()`：
  - 页号唯一；
  - 有空闲页时按既定规则复用；
  - 无空闲页时扩展文件；
  - 更新后的分配状态可在重启后恢复。
- [x] 实现 `free_page(page_id)`：
  - 拒绝非法页号、不存在页和重复释放；
  - 更新持久化空闲页信息；
  - 明确释放页是否清零，并在文档中说明。
- [x] 实现 `read_page(page_id)`：
  - 成功时始终返回恰好 4096 字节；
  - 拒绝读取不存在或已释放的页；
  - 将底层 I/O 异常转换为 `StorageError`。
- [x] 实现 `write_page(page_id, data)`：
  - 只接受 `bytes`；
  - 长度超过 4096 字节时拒绝；
  - 不足 4096 字节时在尾部补零；
  - 只允许覆盖已经分配且未释放的页面。
- [x] 实现安全、可重复调用的 `close()`；关闭后继续操作必须返回明确错误。
- [x] 对关键元数据更新执行刷新，避免“页数据已写入但分配状态未保存”。

阶段验收：

- [x] 连续分配至少 100 个页面，页号不重复。
- [x] 写入长度为 0、1、4095、4096 字节的数据均可读回，返回值长度始终为 4096。
- [x] 4097 字节写入被拒绝，磁盘原内容不被破坏。
- [x] 关闭并重新打开后，页面内容、已分配页和空闲页状态保持正确。
- [x] 释放页按既定规则被安全复用；旧内容不会被误当作新页有效数据。
- [x] 截断文件、错误魔数、错误版本和非法页号均返回稳定的 `STORAGE` 错误码。

---

## 四、阶段 2：实现 Buffer Pool

主要文件：

- `src/minidbms/storage/buffer.py`
- `src/minidbms/storage/page.py`

任务：

- [x] 校验缓存容量必须大于 0。
- [x] 缓存中使用 `PageFrame` 保存页号、4096 字节数据、脏标记和 pin 计数。
- [x] 缓存未命中时从 `FileManager` 加载页面。
- [x] 缓存命中时直接返回已有页面，并正确更新 LRU 访问顺序。
- [x] 实现 FIFO：按页面首次进入缓存的顺序淘汰，命中不改变队列顺序。
- [x] 实现 LRU：淘汰最久未访问页面，读命中和写命中都更新最近使用顺序。
- [x] 缓存满时选择未 pin 的牺牲页；所有页面均被 pin 时返回明确错误。
- [x] 干净页淘汰时不写磁盘。
- [x] 脏页淘汰前必须完整写回磁盘，并增加脏页写回计数。
- [x] 实现单页刷新：只写回指定脏页，成功后清除脏标记。
- [x] 实现全部刷新：写回全部脏页，不能遗漏。
- [x] 防止释放仍在使用或仍被 pin 的页面。
- [x] 使用标准日志模块输出可读的命中、未命中、淘汰和脏页回写日志，没有直接散落 `print()`。
- [x] 实现并维护五项统计；主动刷新计入 `dirty_writes`，文档和测试保持一致。

阶段验收：

- [x] 容量为 2，访问顺序 `0, 1, 0, 2` 时，LRU 淘汰页 1，FIFO 淘汰页 0。
- [x] 重复读取同一页时，命中、未命中和读取请求计数准确。
- [x] 写页后触发淘汰，重新打开文件仍能读到写入内容。
- [x] 干净页淘汰不增加 `dirty_writes`。
- [x] `flush_page` 只刷新目标页；`flush_all` 刷新所有脏页。
- [x] 被 pin 的页面不参与淘汰；无可淘汰页时不会错误覆盖缓存内容。

---

## 五、阶段 3：完成 StorageManager 门面

主要文件：

- `src/minidbms/storage/storage_manager.py`
- `src/minidbms/storage/__init__.py`

任务：

- [x] 在构造函数中创建并连接 `FileManager` 与 `BufferPool`。
- [x] 接受大小写不敏感的 `LRU`、`FIFO`，拒绝其他策略。
- [x] 完整实现公共协议中的八个方法，公开方法不再出现 `NotImplementedError`。
- [x] `read_page()` 始终返回不可变的 `bytes`，避免调用方绕过 `write_page()` 修改缓存。
- [x] `write_page()` 统一完成类型、长度和页状态检查。
- [x] `free_page()` 通过缓存层检查并移除缓存页，同时更新磁盘分配状态，不留下“幽灵页”。
- [x] `stats()` 返回新字典，调用方修改结果不能污染内部统计。
- [x] `close()` 先 `flush_all()` 再关闭文件，并做到幂等。
- [x] 已实现上下文管理器 `__enter__` / `__exit__`；未修改公共协议。

阶段验收：

- [x] 消费方只导入 `StorageManager` 即可完成页分配、读写、刷新、统计和关闭；真实 wzy 引擎调用待联调。
- [x] 消费方不需要访问 `FileManager`、`BufferPool` 或内部缓存字典。
- [x] 对外行为与 `src/minidbms/common/interfaces.py` 中的 `StorageManagerProtocol` 一致。

---

## 六、阶段 4：补齐自动化测试

主要文件：`tests/test_storage.py`

测试必须使用 pytest 的临时目录 `tmp_path`，不得覆盖人工演示数据库。

- [x] 文件首次创建与重启恢复。
- [x] 唯一页号、连续扩展、释放和安全复用。
- [x] 0～4096 字节写入与尾部补零。
- [x] 超长数据、非 bytes 数据、负页号、越界页号、已释放页访问。
- [x] 重复释放和关闭后操作。
- [x] 文件魔数、格式版本、文件长度损坏检测。
- [x] LRU 确切淘汰顺序。
- [x] FIFO 确切淘汰顺序。
- [x] 命中、未命中、读取请求、淘汰和脏写统计。
- [x] 脏页淘汰写回与干净页淘汰不写回。
- [x] `flush_page`、`flush_all` 和 `close` 的持久化行为。
- [x] 释放缓存中的页面后不能再次命中旧内容。
- [x] 两个独立 `StorageManager` 实例使用不同临时文件时互不影响。
- [x] 已加入小容量缓存压力测试和多次重启测试。

建议把测试按三类组织：

```text
TestFileManager
TestBufferPool
TestStorageManager
```

完成标志：`tests/test_storage.py` 不再是空文件，并且每项验收要求都有自动化断言，不只靠人工日志判断。

---

## 七、阶段 5：文档、演示与交接

主要文件：

- `docs/storage_design.md`
- `README.md`

- [x] 在设计文档中画清“StorageManager → BufferPool → FileManager → 数据库文件”的调用关系。
- [x] 记录数据库文件头和物理页布局，包括各字段大小、字节序、页偏移计算方法和格式版本。
- [x] 记录空闲页持久化及重启恢复流程。
- [x] 说明 LRU 与 FIFO 的数据结构、命中更新规则和淘汰差异。
- [x] 列出全部存储错误码和触发条件。
- [x] 列出统计字段的精确定义。
- [x] 已保存容量为 2 的 LRU/FIFO 演示日志至 `docs/storage_demo.md`。
- [x] 已在 `docs/storage_design.md` 提供最小调用示例：创建存储、分配页、写页、读页、查询统计、刷新和关闭。
- [x] wzy 引擎已通过 `StorageManager` 完成 Catalog 与数据页联调，端到端和跨进程重启测试通过（见 PR #3）。
- [x] 已更新 README 中存储模块状态和运行测试命令。
- [x] 已提交 PR #2，并在 PR 中写明测试结果、文件格式以及未改动公共接口。

---

## 八、推荐提交顺序

每次提交只包含一个清晰功能及其测试：

1. `feat(storage): define file header and storage errors`
2. `feat(storage): implement persistent page allocation`
3. `test(storage): cover page lifecycle and restart`
4. `feat(storage): implement buffer pool and fifo`
5. `feat(storage): add lru and dirty page flushing`
6. `test(storage): cover replacement and statistics`
7. `feat(storage): complete storage manager facade`
8. `docs(storage): document file layout and cache behavior`

---

## 九、提交前验证命令

```powershell
python -m pip install -e ".[dev]"
python -m minidbms.sql_compiler.build_native
python -m pytest tests/test_storage.py -q
python -m pytest -q
git status --short
```

要求：

- [x] 存储测试全部通过：36 passed。
- [x] 仓库全部测试通过：100 passed，zby 的编译器测试未受破坏。
- [x] 测试未在仓库中遗留 `.db`、`.dat` 或日志文件。
- [x] `git status` 中没有编译产物、缓存文件或临时数据库。

---

## 十、wzt 模块最终完成标准（Definition of Done）

只有以下项目全部满足，才可把 wzt 模块标记为完成：

- [x] 页面能分配、释放、复用、读取和覆盖写入。
- [x] 任意合法页面写入后可原样读回，短数据按约定补零。
- [x] 页面内容和分配状态在程序重启后保持正确。
- [x] LRU、FIFO 均可选择，并产生正确且不同的淘汰结果。
- [x] 脏页在淘汰、刷新和关闭时可靠写回。
- [x] 五项统计与缓存日志可用于现场演示。
- [x] 非法页号、超长数据、已释放页、损坏文件和 I/O 失败均转换为清晰的 `StorageError`。
- [x] `tests/test_storage.py` 覆盖核心正常流程、边界和异常流程。
- [x] `docs/storage_design.md` 已从提纲补充为可用于实验报告的完整设计说明。
- [x] wzy 已通过公共 `StorageManager` 接口完成真实联调，没有绕过存储层直接操作数据库文件（见 PR #3）。

## 十一、明确不属于 wzt 的任务

- 不解析 SQL、Token 或 AST。
- 不实现 Catalog、表结构、行序列化、槽页记录格式或删除标记。
- 不实现执行算子、WHERE 求值或 CLI。
- 不提前实现 B+ 树、事务、WAL、并发控制或 MVCC。
- 不让存储层理解表名、列名、数据类型或 Python 行对象；存储层只接收页号和 `bytes`。
