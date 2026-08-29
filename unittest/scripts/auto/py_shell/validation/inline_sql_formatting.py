#@<OUT> Default output
+----+-----------------+-----------+-----------+------------+
| id | name            | role      | salary    | hired      |
+----+-----------------+-----------+-----------+------------+
|  1 | Ada Lovelace    | engineer  | 120000.00 | 2021-03-01 |
|  2 | Alan Turing     | engineer  | 135000.00 | 2019-07-15 |
|  3 | Grace Hopper    | manager   | 150000.00 | 2018-01-09 |
|  4 | Edsger Dijkstra | engineer  | 128000.50 | 2022-11-30 |
|  5 | Barbara Liskov  | architect | 145000.25 | 2020-05-20 |
+----+-----------------+-----------+-----------+------------+
5 rows in set ([[*]])

#@<OUT> Vertical output
*************************** 1. row ***************************
    id: 1
  name: Ada Lovelace
  role: engineer
salary: 120000.00
 hired: 2021-03-01
*************************** 2. row ***************************
    id: 2
  name: Alan Turing
  role: engineer
salary: 135000.00
 hired: 2019-07-15
*************************** 3. row ***************************
    id: 3
  name: Grace Hopper
  role: manager
salary: 150000.00
 hired: 2018-01-09
*************************** 4. row ***************************
    id: 4
  name: Edsger Dijkstra
  role: engineer
salary: 128000.50
 hired: 2022-11-30
*************************** 5. row ***************************
    id: 5
  name: Barbara Liskov
  role: architect
salary: 145000.25
 hired: 2020-05-20
5 rows in set ([[*]])

#@<OUT> Tabbed output
id	name	role	salary	hired
1	Ada Lovelace	engineer	120000.00	2021-03-01
2	Alan Turing	engineer	135000.00	2019-07-15
3	Grace Hopper	manager	150000.00	2018-01-09
4	Edsger Dijkstra	engineer	128000.50	2022-11-30
5	Barbara Liskov	architect	145000.25	2020-05-20
5 rows in set ([[*]])

#@<OUT> Tabbed output without headers
1	Ada Lovelace	engineer	120000.00	2021-03-01
2	Alan Turing	engineer	135000.00	2019-07-15
3	Grace Hopper	manager	150000.00	2018-01-09
4	Edsger Dijkstra	engineer	128000.50	2022-11-30
5	Barbara Liskov	architect	145000.25	2020-05-20
5 rows in set ([[*]])

#@<OUT> Full JSON Output
{
    "hasData": true,
    "rows": [
        {
            "id": 1,
            "name": "Ada Lovelace",
            "role": "engineer",
            "salary": 120000,
            "hired": "2021-03-01"
        },
        {
            "id": 2,
            "name": "Alan Turing",
            "role": "engineer",
            "salary": 135000,
            "hired": "2019-07-15"
        },
        {
            "id": 3,
            "name": "Grace Hopper",
            "role": "manager",
            "salary": 150000,
            "hired": "2018-01-09"
        },
        {
            "id": 4,
            "name": "Edsger Dijkstra",
            "role": "engineer",
            "salary": 128000.5,
            "hired": "2022-11-30"
        },
        {
            "id": 5,
            "name": "Barbara Liskov",
            "role": "architect",
            "salary": 145000.25,
            "hired": "2020-05-20"
        }
    ],
    "executionTime": "[[*]]",
    "affectedItemsCount": 0,
    "warningsCount": 0,
    "warnings": [],
    "info": "",
    "autoIncrementValue": 0
}

#@<OUT> Record List JSON Output
[
    {
        "id": 1,
        "name": "Ada Lovelace",
        "role": "engineer",
        "salary": 120000,
        "hired": "2021-03-01"
    },
    {
        "id": 2,
        "name": "Alan Turing",
        "role": "engineer",
        "salary": 135000,
        "hired": "2019-07-15"
    },
    {
        "id": 3,
        "name": "Grace Hopper",
        "role": "manager",
        "salary": 150000,
        "hired": "2018-01-09"
    },
    {
        "id": 4,
        "name": "Edsger Dijkstra",
        "role": "engineer",
        "salary": 128000.5,
        "hired": "2022-11-30"
    },
    {
        "id": 5,
        "name": "Barbara Liskov",
        "role": "architect",
        "salary": 145000.25,
        "hired": "2020-05-20"
    }
]

#@<OUT> Full JSON Output honors --json=raw
{"hasData":true,"rows":[{"id":1,"name":"Ada Lovelace","role":"engineer","salary":120000,"hired":"2021-03-01"},{"id":2,"name":"Alan Turing","role":"engineer","salary":135000,"hired":"2019-07-15"},{"id":3,"name":"Grace Hopper","role":"manager","salary":150000,"hired":"2018-01-09"},{"id":4,"name":"Edsger Dijkstra","role":"engineer","salary":128000.5,"hired":"2022-11-30"},{"id":5,"name":"Barbara Liskov","role":"architect","salary":145000.25,"hired":"2020-05-20"}],"executionTime":"[[*]]","affectedItemsCount":0,"warningsCount":0,"warnings":[],"info":"","autoIncrementValue":0}

#@<OUT> Record List JSON Output honors --json=raw
[{"id":1,"name":"Ada Lovelace","role":"engineer","salary":120000,"hired":"2021-03-01"},{"id":2,"name":"Alan Turing","role":"engineer","salary":135000,"hired":"2019-07-15"},{"id":3,"name":"Grace Hopper","role":"manager","salary":150000,"hired":"2018-01-09"},{"id":4,"name":"Edsger Dijkstra","role":"engineer","salary":128000.5,"hired":"2022-11-30"},{"id":5,"name":"Barbara Liskov","role":"architect","salary":145000.25,"hired":"2020-05-20"}]

#@<OUT> Tabbed output is ignored while --json is enabled
{"hasData":true,"rows":[{"id":1,"name":"Ada Lovelace","role":"engineer","salary":120000,"hired":"2021-03-01"},{"id":2,"name":"Alan Turing","role":"engineer","salary":135000,"hired":"2019-07-15"},{"id":3,"name":"Grace Hopper","role":"manager","salary":150000,"hired":"2018-01-09"},{"id":4,"name":"Edsger Dijkstra","role":"engineer","salary":128000.5,"hired":"2022-11-30"},{"id":5,"name":"Barbara Liskov","role":"architect","salary":145000.25,"hired":"2020-05-20"}],"executionTime":"[[*]]","affectedItemsCount":0,"warningsCount":0,"warnings":[],"info":"","autoIncrementValue":0}

#@<OUT> Batch execution honors the format
id	name	role	salary	hired
1	Ada Lovelace	engineer	120000.00	2021-03-01
2	Alan Turing	engineer	135000.00	2019-07-15
3	Grace Hopper	manager	150000.00	2018-01-09
4	Edsger Dijkstra	engineer	128000.50	2022-11-30
5	Barbara Liskov	architect	145000.25	2020-05-20

#@<OUT> Script read from stdin honors the format
id	name	role	salary	hired
1	Ada Lovelace	engineer	120000.00	2021-03-01
2	Alan Turing	engineer	135000.00	2019-07-15
3	Grace Hopper	manager	150000.00	2018-01-09
4	Edsger Dijkstra	engineer	128000.50	2022-11-30
5	Barbara Liskov	architect	145000.25	2020-05-20

#@<OUT> A \sql one liner honors the format
1	Ada Lovelace	engineer	120000.00	2021-03-01
2	Alan Turing	engineer	135000.00	2019-07-15
3	Grace Hopper	manager	150000.00	2018-01-09
4	Edsger Dijkstra	engineer	128000.50	2022-11-30
5	Barbara Liskov	architect	145000.25	2020-05-20
5 rows in set ([[*]])

#@<OUT> The format letter is not left behind for the next statement
first
1
1 row in set ([[*]])
+--------+
| second |
+--------+
|      2 |
+--------+
1 row in set ([[*]])

#@<OUT> A letter which is not a format starts the next statement
*************************** 1. row ***************************
first: 1
1 row in set ([[*]])
+--------+
| second |
+--------+
|      2 |
+--------+
1 row in set ([[*]])

