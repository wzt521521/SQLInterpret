-- 10 语义错误演示
-- 预期：SEMANTIC:COLUMN_NOT_FOUND

CREATE TABLE demo_error_10(id INT);
SELECT missing_column FROM demo_error_10;
