# MiniDBMS / SQLInterpret

教学型单机 MiniDBMS。核心运行链路已经迁移到 C++17，支持 SQL 编译、页式存储、
Catalog、记录页、执行器和命令行；Python 只保留原 SQL 编译器适配接口、Web 薄服务层
以及相应测试。浏览器前端继续使用原有 HTML/CSS/JavaScript。

## 功能与结构

支持 `CREATE TABLE`、`INSERT`、`SELECT`（投影和可选 `WHERE`）及 `DELETE`。
列类型为有符号 64 位 `INT` 和 UTF-8 `VARCHAR`，`BOOL` 仅供表达式内部使用。

```text
SQL 文本
  -> 原有 C++ SQL Compiler（未改动）
  -> C++ Plan / Expression
  -> C++ Executor / CatalogManager / RowPage
  -> C++ StorageManager / BufferPool / FileManager
  -> 单个 .db 文件
```

| 目录 | 作用 |
| --- | --- |
| `src/minidbms/sql_compiler/` | 原 SQL 编译器及 Python 适配，保持不变 |
| `cpp/include/wzt/`、`cpp/src/storage/` | 从 wzt 模块并入的固定页存储和缓存 |
| `cpp/include/minidbms/engine/`、`cpp/src/engine/` | Catalog、行页、执行器和 Database 门面 |
| `cpp/src/cli/` | C++ 命令行程序 |
| `cpp/src/bridge/` | Web 使用的本地 JSON 桥接程序 |
| `src/minidbms/web/` | Python 标准库 HTTP 薄服务和原前端资源 |
| `cpp/tests/`、`tests/` | CTest 核心测试和 Python 编译器/Web 黑盒测试 |

项目仍不实现 `UPDATE`、JOIN、GROUP BY、索引、事务、WAL、并发控制、MVCC 或
突然断电后的崩溃恢复。同一数据库文件不能被多个写进程同时打开。

## 构建

需要 CMake 3.16+ 和 C++17 编译器。Windows 推荐 MinGW；当前路径不含中文目录，
可以直接构建：

```powershell
cmake -S . -B build-cpp -G "MinGW Makefiles"
cmake --build build-cpp -j 4
ctest --test-dir build-cpp --output-on-failure
```

Linux/macOS 可省略 `-G "MinGW Makefiles"`。

顶层 CMake 直接链接原有 `minisql` 静态库，不复制、不修改 SQL 编译器实现。

## 运行 C++ MiniDBMS

Windows：

```powershell
.\build-cpp\minidb_cli.exe --db demo.db
```

Linux/macOS：

```text
./build-cpp/minidb_cli --db demo.db
```

交互示例：

```text
MiniDB > CREATE TABLE student(id INT, name VARCHAR, age INT);
MiniDB > INSERT INTO student VALUES(1, 'Alice', 20);
MiniDB > SELECT id, name FROM student WHERE age >= 18;
MiniDB > DELETE FROM student WHERE id = 1;
MiniDB > stats
MiniDB > exit
```

常用参数：

```text
--file tests/e2e_demo.sql
--buffer-capacity 2
--replacement-policy FIFO
--cache-log
--stats
```

安装 Python 包后，`python -m minidbms` 和 `minidb` 是兼容启动器，最终仍执行
同一个 C++ `minidb_cli`，不会回退到旧 Python 数据库引擎。

## 运行 Web 演示

Web 前端由轻量 Python HTTP 服务提供，服务端通过 `minidb_core_bridge` 调用 C++ 核心：

```powershell
python -m minidbms.web --db minidb-web.db
```

页面中的“清空数据库”会先把当前数据库保存为带时间戳的备份，再创建空数据库；
编辑器中的 SQL 会被保留，因此固定表名的演示脚本可以立即重新执行。

默认打开 `http://127.0.0.1:8765`。页面支持 SQL 编辑、内置示例、结果表格、
Token/AST/执行计划、分阶段错误、表概览和 Buffer Pool 统计。

服务默认寻找 `build-cpp/minidb_core_bridge.exe`（非 Windows 无 `.exe`）。使用其他
构建目录时可设置 `MINIDB_CORE_BRIDGE` 为桥接程序绝对路径。

## 测试

```powershell
cmake --build build-cpp -j 4
ctest --test-dir build-cpp --output-on-failure
python -m pytest -q
```

测试分工：

- `cpp/tests/test_storage.cpp`：文件头、分配/复用、LRU/FIFO、脏页、pin 和重启。
- `cpp/tests/test_engine.cpp`：记录页、完整 SQL 链路、跨页、删除、Catalog 和重启。
- `tests/test_cpp_runtime.py`：C++ CLI 与 Web bridge 黑盒测试。
- `tests/test_sql*.py`：保持原 SQL 编译器及 Python 适配契约。
- `tests/test_web.py`：真实 HTTP 前端接口连接 C++ 核心。

迁移验证已经确认 Python 旧版和 C++ 新版可双向读取同一个数据库文件；存储文件头、
Catalog 快照和行页格式均保持兼容。

更多设计细节见 [数据库引擎设计](docs/database_engine_design.md)、
[存储设计](docs/storage_design.md)、[SQL 编译器设计](docs/sql_compiler_design.md)。
