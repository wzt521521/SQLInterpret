# wzy 数据库引擎与总集成任务清单

> 项目：MiniDBMS / SQLInterpret  
> 负责人：wzy  
> 整理日期：2026-09-14；实现进度复核：2026-09-14  
> 依据：同目录《实验分工.md》第 5～10 节  
> 目标：让 `CREATE TABLE`、`INSERT`、`SELECT ... WHERE`、`DELETE` 经“编译 → Plan → 执行 → 页面存储”完整运行，关闭并重启后仍可查询。

## 一、当前起点与边界

- [x] `common/` 已有 `CatalogView`、`StorageManagerProtocol`、`PlanNode`、`Expression`、`TableSchema`、`ExecutionResult` 和 `DBError` 公共契约。
- [x] zby 的 C++ SQL 编译器与 Python Plan 适配层已合入 `main`；可用 `SQLCompiler` 生成共享 Plan。
- [x] wzt 的 PR [#2](https://github.com/wzt521521/SQLInterpret/pull/2) 已合入 `main`；wzy 分支基于该合并提交创建。
- [x] `engine/` 的 Catalog、行页、执行器、表达式与 `cli/main.py` 已实现，骨架已移除。
- [x] `tests/test_db.py` 已加入 18 项测试，含端到端和跨进程重启自动断言。

当前引擎测试 **18 passed**、全仓测试 **118 passed**；已提交 [PR #3](https://github.com/wzt521521/SQLInterpret/pull/3)。源代码、设计、演示和报告见项目仓库；剩余两项为现场截图与 wzy 本人的报告审阅。

**职责边界：** wzy 负责 Catalog、行编码、页内记录、执行算子、CLI、测试和最终联调。不要重写 zby 的 Lexer/Parser，不要修改 wzt 的页缓存逻辑；执行器只能消费 `PlanNode`，存储访问只能经过 `StorageManager`，WHERE 不得使用 Python `eval()`。

## 二、阶段 0：先冻结跨模块约定

主要参考：`src/minidbms/common/`、`docs/api_contract.md`、`docs/storage_design.md`。

- [x] 确认表名和列名统一规范化规则（建议小写），Catalog、Plan 和查询输出保持一致。
- [x] 确认 `INT` 的范围、固定宽度和字节序，`VARCHAR` 的 UTF-8 长度前缀、最大长度及空字符串行为，并写入设计文档。
- [x] 已按 wzt 存储设计采用逻辑页 0 作为 Catalog 固定入口：仅在 `PAGE_NOT_FOUND` 时初始化，跨进程重启测试已验证；未修改存储文件头。
- [x] 确定 Catalog 页与普通数据页各自的魔数、版本号、字段布局、溢出/扩展方式和损坏检测策略。
- [x] 确定内部扫描行引用（如 `page_id + slot_id`），使 `Filter` 后的 `Delete` 仍能定位原记录。
- [x] 确定执行错误码、`ExecutionResult` 的列/行/影响行数格式，以及单条 SQL 失败后的基础清理规则。
- [x] 核对 zby 生成的六种 Plan 节点和四种 Expression 节点的字段，不对原始 SQL 再做解析。

**阶段验收：** 最少用一份固定 Plan 样例说明“编译器给什么、执行器读取什么、存储层收到哪些页号与 bytes”；文档中的页格式足以让另一人按字节解析。

## 三、阶段 1：持久化 Catalog

主要文件：`src/minidbms/engine/catalog_manager.py`、`docs/database_engine_design.md`。

- [x] 实现正式 `CatalogManager`，维护表名、列名、列顺序、列类型和每张表的数据页列表；它是**唯一正式表结构数据源**。
- [x] 实现 `CatalogView.table_exists()`、`get_schema()`，供 zby 语义分析器只读查询；不存在的表返回稳定的 `DBError`。
- [x] 新数据库通过 `StorageManager.allocate_page()` 建立固定 Catalog 入口，不能绕过存储层写文件头。
- [x] 将 Catalog 序列化到存储页；已有数据库启动时校验魔数/版本并恢复全部表结构及页映射。
- [x] 支持多张表、多个列和元数据增长；Catalog 超出单页时按约定扩展，不能静默截断。
- [x] CREATE 执行成功后立即写入 Catalog；重复表名、重复列名和不支持的类型清晰报错。
- [x] 持久化新数据页归属关系；重启后 SeqScan 能找到同一张表的全部页面。
- [x] 更新 Catalog 失败时清理本次新分配的页面，并避免向编译器暴露未成功创建的表。

**阶段验收：** 建表后关闭、重启，`get_schema()` 返回原列顺序和类型；多表互不混淆；错误/损坏 Catalog 不被当作空库。

## 四、阶段 2：行编码与页内记录

主要文件：`src/minidbms/engine/record.py`、`storage_engine.py`。

- [x] 实现 `RecordCodec.encode/decode`，按 `TableSchema` 编解码 `INT` 和 `VARCHAR`；值数量、类型、整数范围、字符串长度有明确检查。
- [x] 为普通数据页定义页头及槽/记录边界，单页可容纳多条记录；不得用换行符分隔二进制记录。
- [x] 插入时先尝试已有数据页；剩余空间不足才分配新页，并把页号登记到 Catalog。
- [x] 单条记录超过页面可用空间时返回稳定执行错误，不写入半条记录或遗留未登记页。
- [x] SeqScan 遍历 Catalog 指定的全部数据页，反序列化活跃记录并保留内部行引用。
- [x] DELETE 为记录设置 tombstone；扫描时跳过已删除记录，删除状态经刷新和重启仍保留。
- [x] 所有页修改只通过 `StorageManager.write_page()`；所有页读取只通过 `read_page()`。
- [x] 写出记录跨页、Unicode/空字符串、满页边界和删除后再次扫描的测试。

**阶段验收：** 一页能放多行；插入足量行后跨至少两个页；重启后数据页映射、行内容和删除标记均正确。

## 五、阶段 3：表达式求值与六类 Plan 执行

主要文件：`src/minidbms/engine/expression_eval.py`、`executor.py`。

- [x] `ExpressionEvaluator` 递归处理 `LiteralExpr`、`IdentifierExpr`、`UnaryExpr`、`BinaryExpr`。
- [x] 支持 `=`、`!=`、`>`、`>=`、`<`、`<=`，以及 `AND`、`OR`、`NOT` 和括号形成的树；结果与编译器的类型、优先级规则一致。
- [x] 对未知列、非法类型、未知运算符等返回 `DBError(stage=EXECUTION, ...)`；不调用 `eval()`。
- [x] `CreateTablePlan`：创建表并持久化元数据；执行失败不留下半成品表。
- [x] `InsertPlan`：按 Schema 检查、编码、写页，返回正确 `affected_rows`。
- [x] `SeqScanPlan`：逐页产生未删除行及内部行引用。
- [x] `FilterPlan`：在子计划结果上求值 WHERE，保留满足条件的行引用。
- [x] `ProjectPlan`：严格按目标列顺序返回 `columns` 与 `rows`。
- [x] `DeletePlan`：只标记子计划筛出的行，返回准确影响行数；无 WHERE 时作用于全表。
- [x] SELECT、DELETE 不绕过子 Plan；执行器不读取或重新解析 SQL 文本。
- [x] 为单条 SQL 的常见失败路径补基本清理：不出现半条记录、未登记页或错误的 Catalog 状态。

**阶段验收：** 用手工构造的 Plan 测试每个执行分支，再用 zby 真实生成的 Plan 执行 `CREATE → INSERT → SELECT → DELETE → SELECT`；测试同时断言结果列顺序、行数和影响行数。

## 六、阶段 4：CLI 与顺序编译/执行

主要文件：`src/minidbms/cli/main.py`、`src/minidbms/__main__.py`。

- [x] 提供 `MiniDB >` 提示符、连续 SQL 输入、`quit`/`exit` 退出，并可指定数据库文件和缓存容量/策略。
- [x] 让同一批语句按“编译一条 → 执行一条 → 更新真实 Catalog → 再编译下一条”顺序运行；可使用 `SQLCompiler.compile_and_execute()`。不能在 CREATE 真正执行前，凭空假设表已存在。
- [x] 正确处理跨行输入与字符串/注释中的分号；不要简单地用 `split(';')` 切 SQL。
- [x] SELECT 稳定展示列名及结果行；CREATE、INSERT、DELETE 展示成功信息和影响行数。
- [x] 捕获词法、语法、语义、执行、存储阶段的 `DBError`，显示阶段、错误码和位置；单条错误 SQL 不退出 CLI。
- [x] 正常退出和异常退出时都执行 `flush_all()` 与 `close()`；关闭失败也显示可理解错误。
- [x] 提供缓存 `stats()` 与淘汰日志的可演示入口，不要求修改 wzt 内部缓存字段。

**阶段验收：** 连续输入合法 SQL、错误 SQL、合法 SQL，CLI 仍运行；退出重启后能查到之前成功提交的表和数据。

## 七、阶段 5：自动化测试与三人联调

主要文件：`tests/test_db.py`、`tests/e2e_demo.sql`，必要时增加独立端到端测试文件。

- [x] 单元测试使用 pytest `tmp_path`，不覆盖人工演示数据库；测试 Catalog、RecordCodec、页布局、表达式求值、六类 Plan 和失败清理。
- [x] 先做最小联调：`CREATE TABLE student(id INT, name VARCHAR, age INT)` → 插入 Alice → `SELECT id, name ... WHERE age > 18`，结果从真实存储页读回。
- [x] 执行 `tests/e2e_demo.sql`：首次 SELECT 只返回 Alice、Carol；DELETE 影响 1 行；再次 SELECT 只返回 Bob、Carol。
- [x] 关闭并重启同一数据库文件，确认表结构、数据、删除状态和页映射不变。
- [x] 插入足量记录使其跨页，检查全量扫描结果；用容量为 2 的缓存触发未命中与淘汰，再用能保留热点页的访问序列验证重复查询命中增加。全表页数大于缓存容量时，连续顺序扫描可能每次都未命中，不能强行断言命中增加。
- [x] 覆盖重复 CREATE、未知表/列、INSERT 值数量/类型错误、超大记录、缺分号、未闭合字符串、非法字符；错误阶段正确且 CLI 不崩溃。
- [x] 检查 `test_sql.py`、`test_storage.py`、`test_db.py` 和端到端重启测试全部通过，不能用前两组通过代替数据库引擎验收。
- [x] 已用 zby 的真实 Plan/CatalogView 和 wzt 的真实 StorageManager 完成自动联调；固定页 0、页面写回与缓存统计已验证，公共协议未改动。

**阶段验收：** 从 SQL 文本到页面存储的四类语句全链路自动通过；关机重开仍正确；无测试遗留 `.db`、`.dat`、日志或构建产物。

## 八、阶段 6：文档、演示和提交

- [x] 完成 `docs/database_engine_design.md`：Catalog 根页、元数据扩展、行编码、数据页槽布局、执行数据流、错误码、失败清理和局限。
- [x] 更新 README：数据库启动/退出、数据库文件路径、演示命令、测试命令和当前支持的 SQL 范围。
- [ ] 已固化 `tests/e2e_demo.sql` 的预期输出和可复现演示；现场终端截图仍待 wzy 在答辩环境采集。
- [x] 准备典型失败案例，能解释错误发生于词法、语法、语义、执行还是存储阶段。
- [ ] `docs/实验报告.md` 草稿已完成；wzy 本人仍需审阅、补充姓名学号与个人实践叙述，并准备答辩讲解。
- [x] 已提交 PR #3，描述四类 SQL、持久化验证和全部测试结果。

## 九、提交前验证命令

```powershell
python -m pip install -e ".[dev]"
python -m minidbms.sql_compiler.build_native
python -m pytest tests/test_db.py -q
python -m pytest -q
git diff --check
git status --short
```

- [x] 引擎单元测试、端到端测试和全仓测试全部通过。
- [x] 用同一数据库文件做过至少一次**跨进程重启**验证，不只是在同一对象上重新读取。
- [x] `git status` 无临时数据库、日志、编译产物和缓存文件。

## 十、最终完成标准

只有以下项目全部满足，才可把 wzy 的任务标记为完成：

- [x] Catalog 是唯一正式 Schema 数据源，自己通过 `StorageManager` 持久化且重启可恢复。
- [x] INT/VARCHAR 行编码、单页多行、跨页扩展、tombstone 删除均正确。
- [x] 六类 Plan 均可执行；WHERE 与投影结果、DELETE 影响行数正确。
- [x] 四类 SQL 从文本输入到磁盘页完整贯通，执行器不重新解析 SQL。
- [x] CLI 连续运行、分阶段报错、错误后继续执行、退出刷新均正确。
- [x] 跨页、缓存淘汰、脏页写回、重启恢复和主要异常路径均有自动化断言。
- [x] 设计文档、README、演示脚本、预期输出、实验报告及 PR 齐全。

**本次不要求：** UPDATE、JOIN、GROUP BY、ORDER BY、索引、事务、WAL、并发控制、MVCC、权限系统或完整 MySQL 兼容。
