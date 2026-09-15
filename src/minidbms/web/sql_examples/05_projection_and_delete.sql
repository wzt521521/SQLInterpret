-- 05 投影、条件删除与全表删除
-- 覆盖：列顺序、DELETE WHERE、DELETE 无 WHERE、删除后查询

CREATE TABLE demo_inventory_05(id INT, product VARCHAR, quantity INT);
INSERT INTO demo_inventory_05 VALUES(1, 'Keyboard', 8);
INSERT INTO demo_inventory_05 VALUES(2, 'Mouse', 0);
INSERT INTO demo_inventory_05 VALUES(3, 'Monitor', 3);

SELECT product, id FROM demo_inventory_05 WHERE quantity > 0;
DELETE FROM demo_inventory_05 WHERE quantity = 0;
SELECT * FROM demo_inventory_05;
DELETE FROM demo_inventory_05;
SELECT * FROM demo_inventory_05;
