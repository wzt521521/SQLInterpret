-- 02 插入数据与基础查询
-- 覆盖：INSERT、指定插入列顺序、SELECT *、投影列

CREATE TABLE demo_people_02(id INT, name VARCHAR, age INT);
INSERT INTO demo_people_02 VALUES(1, 'Alice', 20);
INSERT INTO demo_people_02(name, age, id) VALUES('Bob', 17, 2);
INSERT INTO demo_people_02 VALUES(3, 'Carol', 22);

SELECT * FROM demo_people_02;
SELECT name, age FROM demo_people_02;
