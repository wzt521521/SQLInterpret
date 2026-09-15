# C++ 端到端演示记录

构建后运行：

```powershell
.\build-cpp\minidb_cli.exe --db demo.db --file tests\e2e_demo.sql --stats
```

脚本依次完成建表、三次插入、带条件的投影查询、删除和再次查询。预期关键结果：

```text
id | name
1 | Alice
3 | Carol
2 row(s)

1 row(s) deleted

id | name | age
2 | Bob | 17
3 | Carol | 22
2 row(s)
```

随后重新运行同一个数据库文件并输入：

```sql
SELECT * FROM student;
```

仍应得到 Bob 和 Carol 两行，证明 Catalog、数据页和 tombstone 在正常关闭后均已持久化。
自动断言位于 `cpp/tests/test_engine.cpp` 和 `tests/test_cpp_runtime.py`。
