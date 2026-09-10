# MiniDBMS / SQLInterpret

大型平台软件设计实习项目，由 SQL 编译器、页式存储系统和数据库引擎组成。

## 模块与状态

| 模块 | 负责人 | 目录 | 状态 |
| --- | --- | --- | --- |
| SQL 编译器 | zby | `src/minidbms/sql_compiler/` | C++17 核心、Python 适配及独立验收测试 |
| 页式存储与缓存 | wzt | `src/minidbms/storage/` | 初始骨架，待实现 |
| 数据库引擎、CLI 与集成 | wzy | `src/minidbms/engine/`、`src/minidbms/cli/` | 初始骨架，待实现 |

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

独立 CLI 展示 Token、AST、绑定 AST、优化前后 Plan；只记录临时表结构，不执行行操作。
根项目 `minidb` 仍是 wzy 的安装检查入口，完整数据库及持久化有待后续联调。

详细说明：[编译器设计及对接](docs/sql_compiler_design.md)、[文法](docs/grammar.md)、
[固定计划样例](docs/sql_plan_examples.json)、[实验分工](docs/实验分工.md)、[公共协议](docs/api_contract.md)。

## 协作

使用 `feature/sql-compiler-zby`、`feature/storage-wzt`、`feature/database-engine-wzy` 功能分支。
按 [CONTRIBUTING.md](CONTRIBUTING.md) 提交 Pull Request，不直接向 main 推送业务功能。
