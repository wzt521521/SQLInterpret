CREATE TABLE student(id INT, name VARCHAR, age INT);
INSERT INTO student(id, name, age) VALUES (1, 'Alice', 20);
INSERT INTO student(id, name, age) VALUES (2, 'Bob', 17);
INSERT INTO student(id, name, age) VALUES (3, 'Carol', 22);
SELECT id, name FROM student WHERE age >= 18 AND NOT name = 'Bob';
DELETE FROM student WHERE id = 1;
SELECT * FROM student;
