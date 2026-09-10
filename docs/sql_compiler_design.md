# SQL 编译器设计与验收（zby）

## 目录和构建

C++17 实现完整保留在 `src/minidbms/sql_compiler/native/`：
`include/minisql/` 放公共头，`src/` 放词法、LL(1)、语义、计划、优化、表达式求值和格式化实现，
`app/main.cc` 为独立编译演示 CLI，`app/bridge.cc` 为 Python 适配进程。
仓库原定的 `lexer.py/parser.py/semantic.py/planner.py/optimizer.py/compiler.py` 是调用适配层。
没有修改 `common/`、`storage/`、`engine/`、`cli/` 的接口和业务代码。

需要 Python 3.11+ 及 g++（C++17），从仓库根目录执行：

```text
python -m pip install -e ".[dev]"
python -m minidbms.sql_compiler.build_native
python -m pytest -q
```

可用 `CXX` 指定 clang++。Windows 的独立构建也可运行
`src/minidbms/sql_compiler/native/build.ps1`；CMake 用户：

```text
cmake -S src/minidbms/sql_compiler/native -B build/native
cmake --build build/native --config Release
```

适配层默认寻找 native/build 下的 minisql_bridge[.exe]。CMake、多配置构建或部署时，
用 `MINISQL_BRIDGE` 指向实际可执行文件；CLI 测试可用 `MINISQL_CLI` 指定路径。
Python 包携带 C++ 源码，安装后显式构建；导入模块不会自动调用编译器。
不提交 exe/o/a 等平台构建产物。CI 在 Ubuntu 上编译 C++ 后执行全部 pytest。

## 编译链路

1. C++ Lexer 校验 UTF-8、扫描 Token，保留原始词素及源码位置。
2. LL(1) 计算 FIRST/FOLLOW/预测表，使用显式栈识别语法，平坦 CST 逆序构造 AST。
   列表遍历非递归；表达式树深度有界；算术尾部左折叠以保持结合性。
3. SemanticAnalyzer 读取 CatalogView 的结构快照，生成绑定 AST 副本，
   标识符增加 table_name、column_index、data_type；检查名称、类型和 INSERT 完整性。
4. Planner 输出六种逻辑 Plan；SELECT 为 Project→Filter→SeqScan，DELETE 为 Delete→Filter→SeqScan。
   无 WHERE 时省略 Filter；INSERT 的列与值统一重排为 Schema 顺序。
5. Optimizer 生成新树，保留原始计划。实现常量折叠、布尔单位元、左侧短路常量、双重否定，
   删除恒真的 Filter，保留恒假的 Filter。保留可能触发溢出的左操作数，避免错误行为被优化掉。

完整文法、优先级和限制见 [grammar.md](grammar.md)。

## Python 与 C++ 对接

`SQLCompiler.compile(sql_text, catalog)` 保持返回 `list[common.plans.PlanNode]`。
原有公共类和字段没有改变。绑定后的表达式是公共 IdentifierExpr/UnaryExpr/BinaryExpr 的子类，
因此执行器可按 `isinstance` 匹配，并照常读取 name/operator/left/right/operand/value。
子类额外携带类型、表名和列序号，便于调试及类型安全求值。

协议是长度前缀 UTF-8 输入、JSON 输出，使用 subprocess 参数数组及 stdin，
不经过 shell，不通过拼接 CREATE SQL 传输 Catalog，不执行 Python eval。
进程隔离也把 native 崩溃/超时变为可捕获的 DBError；默认单次调用超时 30 秒。
目前每个阶段启动进程，适合教学规模；逐行调用原生求值有进程开销。

仅查询语句实际引用的表。调用者的 CatalogView 是唯一正式元数据来源，
native 接收一次调用的只读快照。编译 CREATE 不登记表。
`compile('CREATE ...; INSERT ...;', catalog)` 不假定 CREATE 已经执行；
若后续表不存在，正确返回 TABLE_NOT_FOUND。需要顺序执行的批次用：

```python
from minidbms.sql_compiler import SQLCompiler

compiler = SQLCompiler()
results = compiler.compile_and_execute(sql_text, catalog_manager, executor)
```

这会先校验整批词法/语法，然后每条依次编译、execute，下一次绑定重新读取正式 Catalog。
执行失败立即停止，不编译后续语句；不提供事务或对已成功执行语句的回滚。
也可 `prepare` 后自行循环 `compile_statement(...).plan_after` 与 `executor.execute`。

公共 InsertPlan.values 要求标量，因此适配时使用原生求值器计算常量 VALUES，
即使 `optimize=False` 也会做该必需转换。INSERT 常量溢出此时以 EXECUTION 阶段返回。
原始表达式仍保留在 `compile_detailed(...)[0].native` 的原生 AST/Plan 中。

WHERE 表达式可以交给 wzy 的求值器，也可使用兼容助手：

```python
from minidbms.sql_compiler import evaluate_expression

# row 必须是 Schema 顺序的完整行；投影后的行不能作为输入。
accepted = evaluate_expression(filter_plan.predicate, row)
```

助手复用 C++ 的 INT64 溢出检查、类型检查、UTF-8 比较和从左到右短路规则。
Python SemanticAnalyzer 返回新的绑定语句列表，应使用返回值传给 PlanGenerator，原 AST 不被改写。
PlanOptimizer 可独立优化公共 Plan；涉及列时必须使用已绑定表达式。

## 输出和错误

`compile_detailed` 返回原 AST、bound_ast、plan_before、plan_after、Token 及 native JSON 数据。
`json.dumps(detail.native, ensure_ascii=False, indent=2)` 可直接用于报告。
独立 CLI 示例（Windows 加 `.exe`）：

```text
src/minidbms/sql_compiler/native/build/minisql_cli --file tests/e2e_demo.sql --format json
src/minidbms/sql_compiler/native/build/minisql_cli --sql "CREATE TABLE t(a INT); SELECT * FROM t WHERE TRUE AND a>10+8;" --trace
```

CLI 中 CREATE 只登记演示会话结构，没有执行器和持久化功能。
Python 桥接不使用此演示登记行为。词法/语法/语义诊断继承 common.DBError，保留 stage/code/location，
并增加 actual/expected。原生 INTERNAL 及进程问题映射为 EXECUTION，避免修改公共枚举。

## 验收与边界

`tests/test_sql_native_cli.py` 覆盖原 C++ 的四类语句、大小写、文法表、错误位置、
注释、UTF-8、INT64_MIN、左结合、超深表达式、交互错误恢复和两项优化。
`tests/test_sql.py` 覆盖公共 Python 类型对接、所有分阶段入口、Catalog 只读、
顺序执行与失败终止、错误转换、优化前后行结果/错误等价、未优化 INSERT 常量转换及缺失程序诊断。

本次本地验证环境为 Windows、Python 3.13.12、MinGW g++ 14.2.0；
C++ 以 `-Wall -Wextra -Wpedantic -Werror` 构建通过，全部 64 项 pytest 通过。
Python wheel 构建及内容检查通过，包含 23 个原生源码/构建文件，不包含平台二进制。
Linux/Python 3.11 的自动构建已加入 CI，结果以推送后 GitHub Actions 为准。

zby 的编译器功能、测试与文档可独立验收。仓库当前 wzt/wzy 的存储、引擎、正式 Catalog
仍为骨架；本次验证使用只读内存 Catalog 和接收公共 Plan 的假 Executor，
不能据此宣称磁盘持久化、正式执行器或小组端到端验收已经完成。
