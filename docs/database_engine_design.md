# 数据库引擎设计与接口交接

负责人：wzy。C++ 实现位于 `cpp/include/minidbms/engine/`、`cpp/src/engine/`
和 `cpp/src/cli/`。

## 1. 运行链路与模块边界

```text
SQL 文本 → zby 的 C++ SQLCompiler → C++ PlanPtr → wzy 的 C++ Executor
                                   ↘ CatalogView ← CatalogManager
                 Executor → StorageEngine / RecordCodec / RowPage
                                      ↓ 页号 + bytes
                              wzt 的 StorageManager
                                      ↓
                               4096 字节数据页
```

`Database::execute(sql_text)` 直接调用原生 `minisql::Compiler::compile_and_execute`，
每执行一条语句后再绑定下一条。运行链路不再经过 Python Plan 转换或编译器子进程。
CREATE 的 Schema 只有在执行成功后才进入正式 Catalog。`Executor` 不重新解析 SQL；
`StorageEngine` 不访问文件管理器或 Buffer Pool 内部成员。

表名和列名统一为小写。编译器已有的 `CatalogView`、`PlanPtr`、`ExprPtr` 和
`ExecutionResult` 接口未改动。SELECT 返回 `columns` 和 `rows`；INSERT/DELETE 返回
`affected_rows` 与信息；执行错误使用原有 `minisql::DBError`。

## 2. Catalog 固定入口与持久化

wzt 的文件头没有 Catalog 根页号。wzy 将**逻辑页 0 固定为 Catalog 根页**：打开数据库时先 `read_page(0)`；仅当返回 `PAGE_NOT_FOUND`（新文件还没有任何逻辑页）时，调用 `allocate_page()` 并要求得到 0。`PAGE_FREED`、错误魔数或版本都表示损坏，不能重新初始化。所有读写均经过 `StorageManager`。

Catalog 是唯一正式表结构数据源，保存表名、列顺序/类型以及每张表的数据页号列表。根页固定 4096 字节，前 24 字节为小端序 `<8sIIII>`：

| 字段 | 大小 | 含义 |
| --- | ---: | --- |
| Magic | 8 | `MDBCAT01` |
| Version | 4 | `1` |
| First chunk page | 4 | 首个快照页号；空 Catalog 为 `0xFFFFFFFF` |
| Snapshot length | 4 | UTF-8 JSON 的总字节数 |
| CRC32 | 4 | 快照字节校验值 |

快照是 `{"tables":[{"name":"student","columns":[["id","INT"],...],"pages":[2,...]}]}`。每个快照页前 16 字节为小端序 `<8sII>`：Magic=`MDBCTCH1`、下一页页号、该页负载长度；其余最多 4080 字节保存快照片段。链表允许元数据跨多个页。启动时校验根页、链、长度、CRC32、表/列和页号结构，错误统一为 `CORRUPT_CATALOG`。

更新采用写时复制：先分配并写入新快照页，逐页刷新，然后更新并刷新页 0 根指针；成功后在内存中切换表信息并释放旧快照页。更新失败时尽量恢复旧根指针并释放本次新分配页。这个设计解决普通执行错误时的半成品元数据问题；**没有 WAL，不保证突然断电时的原子提交或回收全部孤儿页**。同一文件不支持多个进程同时写入。

## 3. 行编码、数据页与删除

`RecordCodec` 依据 Catalog 中的 Schema 编解码：

| 类型 | 编码 | 检查 |
| --- | --- | --- |
| INT | 小端有符号 64 位 `<q>` | 范围 `[-2^63, 2^63-1]`；拒绝 `bool` |
| VARCHAR | 小端无符号 32 位字节长度 `<I>` + UTF-8 内容 | 空串允许；非法 Unicode/截断拒绝 |

单条记录不能超过 `4096 - 16 - 8 = 4072` 字节；超过返回 `RECORD_TOO_LARGE`，不会分配数据页。数据页格式：16 字节页头 `<8sII>`（Magic=`MDBROW01`、Version=`1`、槽数）；每个槽 8 字节 `<II>`（记录偏移、记录长度）。长度最高位是 tombstone；槽从页头后向前增长，记录从页尾向后增长。`RowPage` 检查槽边界、重叠、页版本和记录完整性。删除只设置标记，不立即复用空间；扫描跳过已删除槽。

`StorageEngine.insert` 优先尝试 Catalog 已登记的数据页，放不下时新分配页，先写入完整记录页，再登记页号。登记失败时释放新页。扫描返回内部 `RowRef(table_name, page_id, slot_id)` 与行值，`Filter` 保留行引用，`Delete` 才能准确标记符合条件的原槽。一次 DELETE 影响多行时不提供事务级全有或全无保证。

## 4. 表达式与六类 Plan

`ExpressionEvaluator` 递归计算共享表达式树：字面量、列引用、一元和二元表达式。支持有符号 64 位加减乘及溢出检查、六种比较、`AND`/`OR` 短路与 `NOT`。字符串按 UTF-8 字节序比较，BOOL 只允许等于/不等于。类型错误和未知列返回执行阶段错误；不使用 `eval()`。

| Plan | 执行动作 |
| --- | --- |
| `CreateTablePlan` | 校验并持久化表 Schema |
| `InsertPlan` | 依据列名重排值、编码并写入记录 |
| `SeqScanPlan` | 按 Catalog 页列表逐页读取活跃行 |
| `FilterPlan` | 对子计划行计算 WHERE，保留行引用 |
| `ProjectPlan` | 按目标列顺序输出结果 |
| `DeletePlan` | 对子计划筛出的行设置 tombstone，返回影响数 |

## 5. CLI、错误与关闭

`build-cpp/minidb_cli`（Windows 为 `.exe`）提供 `MiniDB >` 提示符；支持多语句、
跨行输入、`exit`/`quit`、`stats` 命令。输入分帧器只寻找字符串和注释之外的分号，
实际词法、语法、语义仍由 zby 编译器完成。`--file` 执行 UTF-8 SQL 文件，
`--buffer-capacity`/`--replacement-policy` 控制缓存，`--cache-log` 输出淘汰和脏写日志。
一条错误 SQL 不会终止后续语句。C++ RAII 析构和显式 `close()` 都会刷新全部脏页。

典型引擎错误码：`TABLE_EXISTS`、`TABLE_NOT_FOUND`、`CORRUPT_CATALOG`、`CORRUPT_PAGE`、`CORRUPT_RECORD`、`TYPE_MISMATCH`、`VALUE_COUNT_MISMATCH`、`RECORD_TOO_LARGE`、`PAGE_FULL`、`COLUMN_NOT_FOUND`、`INTEGER_OVERFLOW`、`INVALID_PLAN`。zby 的词法/语法/语义错误和 wzt 的存储错误保持各自的原有阶段与错误码。

## 6. 验证与范围

`cpp/tests/test_engine.cpp` 覆盖真实编译器/Plan 的建表、插入、筛选、投影、删除、
跨页、重启和 Catalog 扩展；`tests/test_cpp_runtime.py` 对 C++ CLI、bridge 以及演示库重置做进程级验收。
运行方式见 [README](../README.md)。

项目基线不包含 UPDATE、JOIN、GROUP BY、索引、事务、WAL、并发控制或崩溃恢复。正常关闭后的持久化与基本执行错误清理是本次验收范围。
