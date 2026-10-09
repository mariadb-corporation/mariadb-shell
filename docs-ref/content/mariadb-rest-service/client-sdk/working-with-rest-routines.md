---
description: >-
  Call REST functions and procedures with the MRS client SDK, pass IN and INOUT
  parameters, and read OUT parameters and result sets.
---

# Working with REST Routines

In the MariaDB REST Service (MRS) client SDK, you run a REST function or procedure with the `call()` command. The command takes the set of `IN` and `INOUT` parameters, with their values, that the database routine accepts.

The examples assume the sakila sample database, with its schema, tables, and routines available under a REST service called `myService`.

## Calling a REST Function

Consider the following REST function, based on the `inventory_in_stock` function of the sakila sample database:

```sql
CREATE OR REPLACE REST FUNCTION /inventoryInStock
    ON SERVICE /myService SCHEMA /sakila
    AS sakila.inventory_in_stock
    PARAMETERS MyServiceSakilaInventoryInStockParams {
        pInventoryId: p_inventory_id @IN
    }
    RESULT MyServiceSakilaInventoryInStockResult {
        result: result @DATATYPE("bit(1)")
    }
    AUTHENTICATION NOT REQUIRED;
```

{% tabs %}
{% tab title="TypeScript" %}
In the TypeScript SDK, the first parameter of the command is an object with the `IN` and `INOUT` parameters and their values.

```typescript
myService.sakila.inventoryInStock.call({ pInventoryId: 1 });
// true
```
{% endtab %}

{% tab title="Python" %}
In the Python SDK, the command takes the same parameters and values as keyword arguments.

```python
my_service.sakila.inventory_in_stock.call(p_inventory_id=1)
# true
```
{% endtab %}
{% endtabs %}

## Optional Parameters

Treat all input parameters of functions and procedures as optional. Routine parameters cannot have `NOT NULL` constraints, which makes them nullable by nature, so an optional parameter is a parameter whose value can be `NULL`.

Calling a function or procedure therefore requires no parameter, and functions and procedures are expected to handle `NULL` values at runtime. For example, take the following stored function:

```sql
DELIMITER //
CREATE FUNCTION my_db.my_func (x INT, y INT)
RETURNS BIGINT DETERMINISTIC
BEGIN
  DECLARE sum_result BIGINT DEFAULT 0;
  IF y is NULL THEN
    SET sum_result = x;
  ELSE
    SET sum_result = x + y;
  END IF;
  RETURN sum_result;
END //
DELIMITER ;
```

with the corresponding REST object created as follows:

```sql
CREATE OR REPLACE REST FUNCTION /myFunc ON SERVICE /myService SCHEMA /myDb AS my_db.my_func
  PARAMETERS IMyServiceMyDbMyFuncParams {
    x: x @IN,
    y: y @IN
  }
```

{% tabs %}
{% tab title="TypeScript" %}
```typescript
myService.myDb.myFunc.call() // null
myService.myDb.myFunc.call({ x: 3 }) // 3
myService.myDb.myFunc.call({ x: 3, y: 2 }) // 5
```
{% endtab %}

{% tab title="Python" %}
```python
my_service.my_db.my_func.call() # None
my_service.my_db.my_func.call(x=3) # 3
my_service.my_db.my_func.call(x=3, y=2) # 5
```
{% endtab %}
{% endtabs %}

## Calling a REST Procedure

Procedures produce output either through result sets or through `OUT` and `INOUT` parameters. For example, take the following stored procedure:

```sql
DELIMITER //
CREATE PROCEDURE my_db.my_proc (IN x INT, IN y INT, OUT z INT)
BEGIN
  DECLARE sum_result BIGINT DEFAULT 0;
  IF y is NULL THEN
    SET sum_result = x;
  ELSE
    SET sum_result = x + y;
  END IF;
  SELECT sum_result INTO z;
  SELECT sum_result AS sum_result;
END //
DELIMITER ;
```

with the corresponding REST object created as follows:

```sql
CREATE OR REPLACE REST PROCEDURE /myProc ON SERVICE /myService SCHEMA /myDb AS my_db.my_proc
  PARAMETERS IMyServiceMyDbMyProcParams {
    x: x @IN,
    y: y @IN,
    z: z @OUT
  }
  RESULT MyServiceMyDbMyProcResult {
    sum_result: sum_result @DATATYPE("INT")
  }
```

{% tabs %}
{% tab title="TypeScript" %}
```typescript
myService.myDb.myProc.call() // { resultSets: [{ type: "MyServiceMyDbMySumResult", items: [{ sum_result: null }] }], outParameters: { z: null } }
myService.myDb.myProc.call({ x: 3 }) // { resultSets: [{ type: "MyServiceMyDbMySumResult", items: [{ sum_result: 3 }] }], outParameters: { z: 3 } }
myService.myDb.myProc.call({ x: 3, y: 2 }) // { resultSets: [{ type: "MyServiceMyDbMySumResult", items: [{ sum_result: 5 }] }], outParameters: { z: 5 } }
```
{% endtab %}

{% tab title="Python" %}
```python
my_service.my_db.my_proc.call() # IMrsProcedureResponse(result_sets=[MrsProcedureResultSet(type='MyServiceMyDbMySumResult', items=[{'sum_result': None}])], out_parameters={'z': None}
my_service.my_db.my_proc.call(x=3) # IMrsProcedureResponse(result_sets=[MrsProcedureResultSet(type='MyServiceMyDbMySumResult', items=[{'sum_result': 3}])], out_parameters={'z': 3}
my_service.my_db.my_proc.call(x=3, y=2) # IMrsProcedureResponse(result_sets=[MrsProcedureResultSet(type='MyServiceMyDbMySumResult', items=[{'sum_result': 5}])], out_parameters={'z': 5}
```
{% endtab %}
{% endtabs %}
