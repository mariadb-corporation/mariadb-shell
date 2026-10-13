---
description: >-
  Create the client-side object of a REST service with the MRS client SDK, and
  authenticate and deauthenticate with MRS native or MariaDB internal
  authentication apps.
---

# Working with REST Services

The initializer of a REST service in the MariaDB REST Service (MRS) client SDK returns an object that implements the interface described in the [TypeScript Client API Reference](typescript-client-api.md) and the [Python Client API Reference](python-client-api.md).

## Creating the Service Object

For a REST service available under the root path `/myService`, you create the client-side object as follows:

{% tabs %}
{% tab title="TypeScript" %}
```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();
```
{% endtab %}

{% tab title="Python" %}
```python
from sdk.my_service import *

my_service = MyService()
```
{% endtab %}
{% endtabs %}

To use a custom base URL, pass it to the initializer:

{% tabs %}
{% tab title="TypeScript" %}
```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService("https://localhost:8443/myService");
```
{% endtab %}

{% tab title="Python" %}
```python
from sdk.my_service import *

my_service = MyService(base_url="https://localhost:8443/myService")
# or
my_service = MyService("https://localhost:8443/myService")
```
{% endtab %}
{% endtabs %}

## Authentication

When a REST object requires authentication, your application authenticates in the scope of the corresponding REST service first. A client authenticates through an existing authentication app with a valid username and password, and optionally a vendor ID.

If you don't specify a vendor ID, the SDK looks up the vendor ID of the authentication app, which costs an extra round-trip to the MRS backend.

The MRS SDK for TypeScript and Python supports authentication apps of two vendors: MRS native authentication, and MariaDB internal authentication (vendor `MariaDB Internal`). For more details, see [Authentication and Authorization](../developer-guide/authentication-and-authorization.md).

### MRS Native Authentication

Create an authentication app with [`CREATE REST AUTH APP`](../rest-sql-reference/rest-authentication.md#create-rest-auth-app), link it to the REST service, and create a REST user for it:

```sql
CREATE REST AUTH APP baz VENDOR MRS;

ALTER REST SERVICE /myService ADD AUTH APP baz;

CREATE REST USER "foo"@"baz" IDENTIFIED BY "bar";
```

Then authenticate with the user:

{% tabs %}
{% tab title="TypeScript" %}
```typescript
myService.authenticate({ username: "foo", password: "bar", app: "baz" })
```
{% endtab %}

{% tab title="Python" %}
```python
my_service.authenticate(username="foo", password="bar", app="baz")
```
{% endtab %}
{% endtabs %}

### MariaDB Internal Authentication

In the same way, create an authentication app for MariaDB internal authentication and link it to the REST service:

```sql
CREATE REST AUTH APP qux VENDOR MARIADB;

ALTER REST SERVICE /myService ADD AUTH APP qux;
```

This time, the user is a MariaDB account:

```sql
CREATE USER foo IDENTIFIED BY "bar";
```

You use the API in exactly the same way:

{% tabs %}
{% tab title="TypeScript" %}
```typescript
myService.authenticate({ username: "foo", password: "bar", app: "qux" })
```
{% endtab %}

{% tab title="Python" %}
```python
my_service.authenticate(username="foo", password="bar", app="qux")
```
{% endtab %}
{% endtabs %}

After the authentication succeeds, every valid SDK command on a REST object that requires authentication succeeds as well.

### Authentication Errors

When you don't specify a vendor ID, the client looks up the vendor in the backend by the name of the authentication app. If the authentication app does not exist, the command fails:

```typescript
try {
    await myService.authenticate({ username: "foo", password: "bar", app: "<non_existing>" })
} catch (err) {
    console.log(err.message) // "Authentication failed. The authentication app does not exist."
}
```

When you specify a vendor ID, the client does not look up the vendor. It assumes that the authentication app belongs to that vendor and authenticates with the mechanism of that vendor. If the app belongs to a different vendor, the authentication fails and the command returns an error:

```typescript
const result = await myService.authenticate({
    username: "foo",
    password: "bar",
    app: "<app_from_different_vendor>",
    vendor: "<vendor_id>"
})

console.log(result.errorMessage) // Authentication failed. The authentication app is of a different vendor.
```

The command also fails when the password does not match the username.

### Deauthentication

An authenticated user logs out of a REST service with the `deauthenticate` command:

```typescript
await myService.deauthenticate()
```

If no user is authenticated, the command fails:

```typescript
try {
    await myService.deauthenticate()
} catch (err) {
    console.log(err.message) // No user is currently authenticated.
}
```
