# 交接清单说明

本目录保留迁移前按 Python 模块划分的个人任务和验收记录，用于回顾原始分工，
其中的 `.py` 路径不再代表当前实现位置。

当前对应关系：

- wzt 存储：`cpp/include/wzt/`、`cpp/src/storage/`、`cpp/tests/test_storage.cpp`
- wzy 引擎和 CLI：`cpp/include/minidbms/engine/`、`cpp/src/engine/`、
  `cpp/src/cli/`、`cpp/tests/test_engine.cpp`
- zby SQL 编译器：仍位于 `src/minidbms/sql_compiler/`，未在本次迁移中修改

构建、运行和最新测试命令见仓库根目录 `README.md`。
