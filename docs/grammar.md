# MiniSQL 文法

负责人：zby

本文件应记录代码实际支持的 SQL 子集文法。以下仅为初始范围，zby 开发 Parser 时需要补充完整产生式、优先级、结合性和错误处理约定。

## 必须支持

- `CREATE TABLE`
- `INSERT INTO ... VALUES ...`
- `SELECT ... FROM ... [WHERE ...]`
- `DELETE FROM ... [WHERE ...]`
- 比较运算：`=`、`!=`、`>`、`>=`、`<`、`<=`
- 逻辑运算：`AND`、`OR`、`NOT`
- 括号表达式

表达式优先级：括号 > `NOT` > 比较运算 > `AND` > `OR`。
