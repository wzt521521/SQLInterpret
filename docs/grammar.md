# MiniSQL 文法（zby，C++17 LL(1)）

实现：`src/minidbms/sql_compiler/native/src/ll1.cc`。
关键字不区分大小写；标识符为 `[A-Za-z_][A-Za-z_0-9]*`，绑定时转为小写。
整数为十进制数字；字符串使用单引号，`''` 表示一个单引号，保留 UTF-8 和换行。
支持 `--` 行注释及不嵌套的 `/* ... */` 块注释。接受 UTF-8 BOM。

## 完整产生式

以下 `ε` 表示空串；标点和大写关键字是终结符，`EOF` 是词法结束标记。
预测表根据以下产生式动态计算 FIRST/FOLLOW/SELECT，冲突直接报错。

```text
Program        -> Statement ; Program | ε
Statement      -> Create | Insert | Select | Delete
Create         -> CREATE TABLE IDENTIFIER ( ColumnDef ColumnDefTail )
ColumnDef      -> IDENTIFIER TypeName
ColumnDefTail  -> , ColumnDef ColumnDefTail | ε
TypeName       -> INT | VARCHAR | BOOL | IDENTIFIER
Insert         -> INSERT INTO IDENTIFIER InsertColumns VALUES ( Expr ExprListTail )
InsertColumns  -> ( ColumnList ) | ε
Select         -> SELECT SelectList FROM IDENTIFIER WhereOpt
SelectList     -> * | ColumnList
Delete         -> DELETE FROM IDENTIFIER WhereOpt
ColumnList     -> IDENTIFIER ColumnListTail
ColumnListTail -> , IDENTIFIER ColumnListTail | ε
WhereOpt       -> WHERE Expr | ε
ExprListTail   -> , Expr ExprListTail | ε
Expr           -> And OrTail
OrTail         -> OR And OrTail | ε
And            -> Not AndTail
AndTail        -> AND Not AndTail | ε
Not            -> NOT Not | Comparison
Comparison     -> Add ComparisonTail
ComparisonTail -> CompareOp Add | ε
CompareOp      -> = | != | > | >= | < | <=
Add            -> Multiply AddTail
AddTail        -> + Multiply AddTail | - Multiply AddTail | ε
Multiply       -> Unary MultiplyTail
MultiplyTail   -> * Unary MultiplyTail | ε
Unary          -> + Unary | - Unary | Primary
Primary        -> INTEGER | STRING | TRUE | FALSE | IDENTIFIER | ( Expr )
```

每条语句必须以分号结尾；允许空输入、注释输入及多语句，不允许单独空分号。
CREATE 的 BOOL/未知类型先进入 AST，再在语义阶段拒绝；实际表列只支持 INT、VARCHAR。
不支持 VARCHAR(n)、NULL、默认值、多行 VALUES、别名、限定列名、除法、JOIN、UPDATE 等扩展。

## 优先级与结合性

从高到低：括号、一元 `+ -`、`*`、二元 `+ -`、比较、`NOT`、`AND`、`OR`。
二元算术及 AND/OR 左结合，NOT 和一元符号右结合，比较不能链式连接。
例如 `a=1 OR b=2 AND NOT c=3` 解析为 `(a=1) OR ((b=2) AND NOT(c=3))`。
这里按验收 SQL 的含义，让 NOT 作用于完整比较；初始分工文档中
“NOT 高于比较”的文字顺序与该验收样例不一致，以本文及实际产生式为准。

## 语义及错误

INT 为有符号 64 位，允许 `-9223372036854775808`。整数越界在语义阶段拒绝，
运行期加减乘/取负溢出报告 EXECUTION/INTEGER_OVERFLOW。字符串按无符号 UTF-8 字节比较。
比较两侧类型须一致；BOOL 仅允许 `=`、`!=`；逻辑运算和 WHERE 要求 BOOL。
INSERT 必须提供所有列，允许调整列顺序，不允许重复列或引用列的 VALUES 表达式。

所有 Token 保留原始 lexeme、从 1 开始的行列位置；CRLF 计为一次换行，UTF-8 列按字符计数，tab 计一列。
语法错误包含 actual/expected；词法、语法、语义错误均有 stage/code/location。
输入上限 1,000,000 字节、100,000 Token、整数 1,000 位、表达式深度/嵌套 128。
错误先完成词法和语法检查，再绑定；CLI 捕获单条错误后可继续输入。

运行 `minisql_cli --ll1` 输出完整产生式、FIRST、FOLLOW 和预测表；
`--trace` 输出预测分析栈、前瞻符号和产生式选择（最多 20,000 步）。
