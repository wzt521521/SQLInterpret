-- 04 布尔条件、括号与算术优化
-- 覆盖：AND、OR、NOT、括号、加减乘、一元正负号、常量折叠、布尔化简

CREATE TABLE demo_orders_04(id INT, customer VARCHAR, amount INT);
INSERT INTO demo_orders_04 VALUES(1, 'Alice', 120);
INSERT INTO demo_orders_04 VALUES(2, 'Bob', 45);
INSERT INTO demo_orders_04 VALUES(3, 'Carol', 80);
INSERT INTO demo_orders_04 VALUES(4, 'David', 20);

SELECT id, customer FROM demo_orders_04
WHERE TRUE AND amount >= 10 * 8;

SELECT * FROM demo_orders_04
WHERE (amount > 100 OR amount = 80) AND NOT customer = 'Bob';

SELECT id, amount FROM demo_orders_04
WHERE amount + 20 >= 100 AND amount - 5 < 120;

SELECT customer FROM demo_orders_04
WHERE +amount >= -(-80);
