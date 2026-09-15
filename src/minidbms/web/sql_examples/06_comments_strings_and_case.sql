-- 06 注释、字符串与大小写兼容
-- 覆盖：单行注释、块注释、字符串内分号、单引号转义、关键字大小写

/* 表名和列名统一按项目规则处理。 */
CrEaTe TaBlE demo_notes_06(id INT, content VARCHAR);

-- 分号在字符串中，不会被当成语句结束符
InSeRt InTo demo_notes_06 VaLuEs(1, 'first; note');
INSERT INTO demo_notes_06 VALUES(2, 'Tom''s book');
INSERT INTO demo_notes_06 VALUES(3, '中文内容');

SeLeCt id, content FrOm demo_notes_06 WhErE id >= 1;
