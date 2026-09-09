# MiniDBMS

大型平台软件设计实习项目骨架。本项目由 SQL 编译器、页式存储系统和数据库引擎三个模块组成。

## 项目状态

当前为团队初始骨架，只包含公共类型、接口约定、模块空壳和基础测试。三个业务模块尚未实现，成员应在各自分支开发并通过 Pull Request 合并。

## 模块与负责人

| 模块 | 负责人 | 目录 |
| --- | --- | --- |
| SQL 编译器 | zby | `src/minidbms/sql_compiler/` |
| 页式存储与缓存 | wzt | `src/minidbms/storage/` |
| 数据库引擎、CLI 与集成 | wzy | `src/minidbms/engine/`、`src/minidbms/cli/` |

`src/minidbms/common/` 是共享接口区。公共类型或接口发生变化时，必须通知全组并单独提交 Pull Request。

## 环境准备

要求 Python 3.11 或更高版本。

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -e ".[dev]"
```

## 运行

当前 CLI 仅用于确认项目安装成功：

```powershell
minidb
```

开发完成后，CLI 应进入 `MiniDB >` 交互界面。

## 测试

```powershell
pytest
```

## 推荐分支

```text
feature/sql-compiler-zby
feature/storage-wzt
feature/database-engine-wzy
```

请勿直接向 `main` 分支提交业务功能。详细职责、验收条件和模块对接方式参见 `docs/实验分工.md` 与 `docs/api_contract.md`。
