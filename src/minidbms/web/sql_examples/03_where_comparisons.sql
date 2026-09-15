-- 03 WHERE 与六种比较运算
-- 覆盖：=、!=、>、>=、<、<=

CREATE TABLE demo_scores_03(id INT, name VARCHAR, score INT);
INSERT INTO demo_scores_03 VALUES(1, 'Ava', 60);
INSERT INTO demo_scores_03 VALUES(2, 'Ben', 75);
INSERT INTO demo_scores_03 VALUES(3, 'Cora', 90);

SELECT name FROM demo_scores_03 WHERE score = 75;
SELECT name FROM demo_scores_03 WHERE score != 75;
SELECT name FROM demo_scores_03 WHERE score > 75;
SELECT name FROM demo_scores_03 WHERE score >= 75;
SELECT name FROM demo_scores_03 WHERE score < 75;
SELECT name FROM demo_scores_03 WHERE score <= 75;
