-- 07 全功能综合演示
-- 一次展示编译、执行、持久化存储、查询过滤和删除标记

CREATE TABLE demo_student_07(id INT, name VARCHAR, age INT);
INSERT INTO demo_student_07 VALUES(1, 'Alice', 20);
INSERT INTO demo_student_07 VALUES(2, 'Bob', 17);
INSERT INTO demo_student_07 VALUES(3, 'Carol', 22);
INSERT INTO demo_student_07(name, age, id) VALUES('Dylan', 19, 4);

SELECT id, name FROM demo_student_07
WHERE age >= 18 AND NOT name = 'Dylan';

SELECT * FROM demo_student_07
WHERE age < 18 OR (age >= 20 AND id != 3);

DELETE FROM demo_student_07 WHERE id = 1;
SELECT * FROM demo_student_07;
