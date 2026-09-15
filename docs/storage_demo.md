# C++ Buffer Pool 演示

构建并运行存储测试：

```powershell
cmake --build build-cpp -j 4
.\build-cpp\test_storage_cpp.exe
```

也可以让完整数据库输出缓存事件：

```powershell
.\build-cpp\minidb_cli.exe --db cache-demo.db --buffer-capacity 2 `
  --replacement-policy LRU --cache-log --file tests\e2e_demo.sql --stats
```

将 `LRU` 改为 `FIFO` 可比较相同访问序列下的淘汰行为。统计包含
`read_requests`、`cache_hits`、`cache_misses`、`evictions` 和 `dirty_writes`。

具体文件格式、dirty/pin 语义和错误码见 [存储设计](storage_design.md)。
