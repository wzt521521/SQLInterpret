# MiniDBMS 端到端演示记录

运行 `python examples/e2e_demo.py`。脚本在系统临时目录建库，分别启动两个独立的 `python -m minidbms` 进程，退出后删除临时文件。第一进程执行 `tests/e2e_demo.sql`，第二进程重新打开同一库查询。

## 第一进程：建表、插入、查询与删除

```text
table student created
1 row inserted
1 row inserted
1 row inserted
id | name
1 | Alice
3 | Carol
2 row(s)
1 row(s) deleted
id | name | age
2 | Bob | 17
3 | Carol | 22
2 row(s)
{'read_requests': 9, 'cache_hits': 7, 'cache_misses': 2, 'evictions': 3, 'dirty_writes': 6}
```

缓存容量为 2。未命中、淘汰和脏写均非零，说明查询和 Catalog/数据页写入实际经过 Buffer Pool。

## 第二进程：关闭后重启

```text
id | name | age
2 | Bob | 17
3 | Carol | 22
2 row(s)
```

表结构、Bob 和 Carol 的数据、Alice 的删除标记均在重新启动后保持正确。

## 编译器与优化证据

同一数据库上编译 `SELECT id, name FROM student WHERE TRUE AND age >= 10+8;`：

```text
tokens: SELECT id , name FROM student WHERE TRUE AND age >= 10 + 8 ;
AST: SelectStmt
semantic: PASS
plan before: Project(id,name) → Filter(TRUE AND age >= 10+8) → SeqScan(student)
plan after:  Project(id,name) → Filter(age >= 18) → SeqScan(student)
```

前后 Plan 分别来自 `CompilationResult.plan_before` 与 `plan_after`。`TRUE AND` 被布尔化简，`10+8` 被常量折叠；执行器消费优化后的公共 Plan 节点。

完整可复现脚本、自动断言和存储页格式分别见 `examples/e2e_demo.py`、`tests/test_db.py`、`docs/database_engine_design.md`。
