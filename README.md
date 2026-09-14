# MiniDBMS / SQLInterpret

大型平台软件设计实习项目，由 SQL 编译器、页式存储系统和数据库引擎组成。

## 模块与状态

| 模块 | 负责人 | 目录 | 状态 |
| --- | --- | --- | --- |
| SQL 编译器 | zby | `src/minidbms/sql_compiler/` | C++17 核心、Python 适配及独立验收测试 |
| 页式存储与缓存 | wzt | `src/minidbms/storage/` | 页文件、Buffer Pool 与统一 StorageManager 已实现并有独立测试；待 wzy 的真实 Catalog 联调 |
| 数据库引擎、CLI 与集成 | wzy | `src/minidbms/engine/`、`src/minidbms/cli/` | Catalog、行页、六类 Plan、CLI 和端到端测试已实现 |

zby 的原 C++ 文件保留在 `src/minidbms/sql_compiler/native/`，通过适配层输出
`common/` 中已有的 Python Plan 和 Expression；其他成员可继续使用现有 Python 接口。
`common/` 是共享接口区，修改需要全组确认并单独提交 PR。

## 环境与测试

需要 Python 3.11+ 和 C++17 编译器 g++（或通过 `CXX` 指定 clang++）。

```text
python -m venv .venv
# Windows PowerShell: .\.venv\Scripts\Activate.ps1
# Linux/macOS: source .venv/bin/activate
python -m pip install -e ".[dev]"
python -m minidbms.sql_compiler.build_native
python -m pytest -q
```

测试前需构建 C++；源码随 Python 包分发，导入时不会自动编译。
Windows 可执行文件带 `.exe`，Linux/macOS 不带。

## 编译器使用

```python
from minidbms.sql_compiler import SQLCompiler

# catalog 是 wzy 提供的 CatalogView；executor 消费公共 Plan 对象。
plans = SQLCompiler().compile("SELECT * FROM student;", catalog)
result = executor.execute(plans[0])

# 含 CREATE 依赖的批次：执行一条后，再绑定下一条。
results = SQLCompiler().compile_and_execute(sql_text, catalog, executor)
```

独立编译演示（Windows 在程序名后加 `.exe`）：

```text
src/minidbms/sql_compiler/native/build/minisql_cli --file tests/e2e_demo.sql --format json
src/minidbms/sql_compiler/native/build/minisql_cli --ll1
```

独立编译器 CLI 展示 Token、AST、绑定 AST、优化前后 Plan；其临时 Catalog 不执行行操作。完整数据库通过下方的 `python -m minidbms` 或 `minidb` 运行。

## 运行完整 MiniDBMS

支持 `CREATE TABLE`、`INSERT`、`SELECT`（可带 WHERE）、`DELETE`（可带 WHERE），数据类型为有符号 64 位 `INT` 和 `VARCHAR`，支持比较、算术、`AND`、`OR`、`NOT`、括号。表结构、数据和删除标记在正常关闭后持久化。

```text
python -m minidbms --db demo.db
MiniDB > CREATE TABLE student(id INT, name VARCHAR, age INT);
MiniDB > INSERT INTO student VALUES(1, 'Alice', 20);
MiniDB > SELECT id, name FROM student WHERE age >= 18;
MiniDB > DELETE FROM student WHERE id = 1;
MiniDB > stats
MiniDB > exit
```

可以使用 `--file tests/e2e_demo.sql` 运行固定演示，`--buffer-capacity 2 --replacement-policy FIFO --cache-log --stats` 查看淘汰日志与统计。`exit`/`quit` 会刷新脏页并关闭文件；再次指定相同 `--db` 路径可验证持久化。错误以 `阶段:错误码:原因` 显示，CLI 会继续接收后续 SQL。数据库 API 示例：

```python
from minidbms.engine import Database

with Database("demo.db", buffer_capacity=2) as db:
    results = db.execute("CREATE TABLE t(id INT); INSERT INTO t VALUES(1); SELECT * FROM t;")
    print(results[-1].rows)  # [[1]]
```

`python examples/e2e_demo.py` 会在临时目录里运行两个独立进程，完成四类 SQL、重启查询和编译器输出展示；完整结果见[演示记录](docs/e2e_demo_result.md)。引擎页格式、Catalog 根页和异常边界见[数据库引擎设计](docs/database_engine_design.md)。本项目不实现事务、WAL、并发控制或突然断电后的崩溃恢复。

## 存储模块使用与验证

`StorageManager` 只接受页号与 `bytes`，页大小固定为 4096 字节。短数据尾部补零；
可在构造时选择 `LRU` 或 `FIFO` 和缓存容量。退出上下文时会刷新脏页并关闭文件。

```python
from minidbms.storage import StorageManager

with StorageManager("demo.db", buffer_capacity=2, replacement_policy="LRU") as storage:
    page_id = storage.allocate_page()
    storage.write_page(page_id, b"hello")
    assert storage.read_page(page_id).startswith(b"hello")
    print(storage.stats())
    storage.flush_all()
```

```text
python -m pytest tests/test_storage.py -q
python -m pytest tests/test_db.py -q
python -m pytest -q
python examples/storage_demo.py
```

演示脚本只在系统临时目录创建数据库文件。文件格式、错误码、接口交接与
[LRU/FIFO 演示日志](docs/storage_demo.md)见[存储设计说明](docs/storage_design.md)。

详细说明：[编译器设计及对接](docs/sql_compiler_design.md)、[文法](docs/grammar.md)、
[固定计划样例](docs/sql_plan_examples.json)、[实验分工](docs/实验分工.md)、[公共协议](docs/api_contract.md)。

## 协作

使用 `feature/sql-compiler-zby`、`feature/storage-wzt`、`feature/database-engine-wzy` 功能分支。
按 [CONTRIBUTING.md](CONTRIBUTING.md) 提交 Pull Request，不直接向 main 推送业务功能。
