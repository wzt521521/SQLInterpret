# 协作规范

1. 从最新 `main` 创建自己的功能分支。
2. 仅修改本人负责的模块；共享目录的修改需要先在组内确认。
3. 每个功能同时提交对应测试，不以手工运行代替自动化测试。
4. 提交前运行 `pytest`，确保全部测试通过。
5. 通过 Pull Request 合并到 `main`，禁止强制推送和直接覆盖他人提交。
6. 提交信息使用简短、明确的格式，例如：

```text
feat(storage): implement page allocation
test(sql): cover unterminated string error
fix(engine): skip deleted records during scan
docs(api): clarify page write contract
```
