# 模块对接协议

本文件记录三个模块共同遵守的最小稳定接口。对应代码位于 `src/minidbms/common/`。修改前必须由三人确认。

## 编译器到执行引擎

- zby 的编译器输出结构化 Plan，wzy 的执行器消费 Plan。
- 执行器不得重新解析原始 SQL。
- WHERE 条件必须使用 Expression 节点，禁止使用 Python `eval()`。
- SELECT 通过 ExecutionResult 的 columns 和 rows 返回结果。

## Catalog 查询

- wzy 提供正式且持久化的 CatalogManager。
- zby 只通过 CatalogView 查询表是否存在以及表结构。
- CREATE 编译阶段不修改 Catalog，执行成功后再由 wzy 持久化。

## 引擎到存储系统

- wzt 提供 StorageManager，页大小固定为 4096 字节。
- read_page 始终返回 4096 字节。
- write_page 只接收 bytes；不足一页按约定补零，超过一页报错。
- wzy 负责行、表和 Catalog 的页内格式，wzt 不解析页面内容。
- wzy 不直接打开数据库文件，也不访问 Buffer Pool 内部结构。

## 错误处理

- 所有用户可见错误继承 DBError。
- stage 使用 LEXICAL、SYNTAX、SEMANTIC、EXECUTION 或 STORAGE。
- 自动化测试优先断言稳定的 stage 和 code。

## 版本管理

公共接口发生不兼容变更时，必须同时更新代码、本文档和三个模块的相关测试。
