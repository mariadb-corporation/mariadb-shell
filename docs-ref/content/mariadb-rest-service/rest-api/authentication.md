---
description: >-
  Authenticate a REST user against a REST service of the MariaDB REST Service
  over HTTP, with MariaDB internal, MRS, or OAuth authentication, and send the
  session cookie or bearer token with later requests.
---

# Authenticating REST Users

Before you execute CRUD operations on a REST object of the MariaDB REST Service (MRS) that requires authentication, you authenticate a REST user through one of the authentication apps linked to the REST service. The MariaDB REST Daemon handles the authentication requests. The workflow depends on the vendor of the authentication app. See [CREATE REST AUTH APP](../rest-sql-reference/rest-authentication.md#create-rest-auth-app) for how to create an authentication app.

## MariaDB Internal Authentication

An authentication app with MariaDB internal authentication, vendor `MySQL Internal`, authenticates the REST user with a MariaDB account. The client sends the credentials to the `/login` path of the authentication endpoint of the REST service, by default `/${service}/authentication/login`. The client specifies the authentication mechanism to use, cookie or bearer token. The default is a cookie.

Pattern:

```text
POST http://<HOST>:<PORT>/<ServiceAlias>/<AuthPath>/login
{
    "username": "...",
    "password": "...",
    "authApp": "...",
    "sessionType": "cookie | bearer"
}
```

If the client requests a cookie and the authentication succeeds, the MariaDB REST Daemon immediately sends back a response with the corresponding `Set-Cookie` header.

Example:

```sh
curl -i -X POST -d \{\"username\":\"...\",\"password\":\"...\",\"authApp\":\"MariaDB\",\"sessionType\":\"cookie\"} https://localhost:8000/mrs/authentication/login
HTTP/1.1 200 Ok
...
Set-Cookie: session_31000000000000000000000000000000=XXXXXXXXXXXXXXXXXXX; Path=/; SameSite=None; Secure; HttpOnly
```

If the client requests a bearer token, the response has no `Set-Cookie` header. Instead, its body is a JSON object with an `accessToken` property whose value is the generated JWT.

Example:

```sh
curl -i POST -d \{\"username\":\"...\",\"password\":\"...\",\"authApp\":\"MariaDB\",\"sessionType\":\"bearer\"\} "https://localhost:8443/mrs/authentication/login"
HTTP/1.1 200 Ok
...
Content-Type: application/json
...

{"accessToken":"..."}
```

## MRS Authentication

An authentication app with the vendor `MRS` follows a [SCRAM](https://datatracker.ietf.org/doc/html/rfc5802)-style negotiation. First, the client sends the credentials to the authentication endpoint. Instead of the password, the client generates and sends the initial nonce, a random hex string. The client also specifies the authentication mechanism to use, cookie or bearer token. The MariaDB REST Daemon responds with a JSON object in the body that includes the values of all variables required to calculate the SCRAM client proof.

Pattern:

```text
POST http://<HOST>:<PORT>/<ServiceAlias>/<AuthPath>/login
{
    "username": "...",
    "nonce": "...",
    "authApp": "...",
    "sessionType": "cookie | bearer"
}

POST http://<HOST>:<PORT>/<ServiceAlias>/<AuthPath>/login
{
    "clientProof": [...],
    "nonce": "...",
    "state": "response"
}
```

Example:

```sh
curl -i -X POST -d \{\"username\":\"...\",\"nonce\":\"...\",\"authApp\":\"MRS\",\"sessionType\":\"cookie\"} https://localhost:8000/mrs/authentication/login
...
{"session":"...","iterations":5000,"nonce":"...","salt":[...]}
```

To finish the SCRAM negotiation, the client calculates the client proof and sends another request to the same endpoint with that value, the server-generated nonce, and a `state` flag set to `response`. If the authentication succeeds, the MariaDB REST Daemon sends back a response with the `Set-Cookie` header.

Example:

```sh
curl -i -X POST -d \{\"clientProof\":[...],\"nonce\":\"...\",\"state\":\"response\"\} https://localhost:8000/mrs/authentication/login
HTTP/1.1 200 Ok
...
Set-Cookie: session_30000000000000000000000000000000=XXXXXXXXXXXXXXXXXXX; Path=/; SameSite=None; Secure; HttpOnly
```

If the client requests a bearer token instead, the process is the same apart from the last step, where the MariaDB REST Daemon sends back a JSON object in the body with an `accessToken` property whose value is the generated JWT.

## OAuth

An OAuth authentication app requires a hypermedia client like a web browser, because in the initial step the MariaDB REST Daemon redirects the client to the authentication web page of the OAuth provider, for example Google or Facebook. OAuth supports cookie-based authentication only.

Example:

```sh
curl -iG https://localhost:8443/mrs/authentication/login --data-urlencode 'authApp=Facebook'
HTTP/1.1 307 Temporary Redirect
...
Location: https://www.facebook.com/v12.0/dialog/oauth?response_type=code&state=first&client_id=XXXXXXXXXXXXX&redirect_uri=https://localhost:8443/mrs/authentication/login?authApp=Facebook
```

If the authentication succeeds, the OAuth provider redirects the client to the callback URL, which sends the authorization code to the MariaDB REST Daemon. The MariaDB REST Daemon redirects the client to a different URL under its control and then sends back a response with the `Set-Cookie` header.

Example:

```sh
curl -i https://localhost:8443/mrs/authentication/login --data-urlencode 'access_token=YYYYYYYYYYYY' --data-urlencode 'redirect_url=https://localhost:8443/mrs/success'
HTTP/1.1 307 Temporary Redirect
...
Location: https://www.facebook.com/v12.0/dialog/oauth?response_type=code&state=first&client_id=XXXXXXXXXXXXX&redirect_uri=https://localhost:8443/mrs/authentication/login?authApp=Facebook


curl -i https://localhost:8443/mrs/success
HTTP/1.1 200 Ok
...
Set-Cookie: session_32000000000000000000000000000000=XXXXXXXXXXXXXXXXXXX; Path=/; SameSite=None; Secure; HttpOnly
```

{% hint style="info" %}
The examples are for illustration only. The hypermedia client, the web browser, handles the whole workflow.
{% endhint %}

## Executing CRUD Operations on a REST Object

After the authentication succeeds, you execute the CRUD operations enabled for a REST object that requires authentication by including an additional header in the HTTP request. With cookie-based authentication, use the value of the `Set-Cookie` response header in the `Cookie` request header.

Pattern:

```text
Cookie: <Set-Cookie>
```

Example:

```sh
curl -H "Cookie: session_31000000000000000000000000000000=XXXXXXXXXXXXXXXXXXX; Path=/; SameSite=None; Secure; HttpOnly" https://localhost:8443/mrs/sakila/actor
```

With token-based authentication, put the JWT in the `Authorization` request header:

Pattern:

```text
Authorization: Bearer XXXXXXXXXXXXXXXXX
```

Example:

```sh
curl -H "Authorization: Bearer XXXXXXXXXXXXXXXXX" https://localhost:8443/mrs/sakila/actor
```

### Full Workflow Example

When the user is not authenticated yet, a CRUD operation on a protected REST object takes two steps that share state: you save the cookie or the bearer token between them.

When you request a cookie, extract the value of the `Set-Cookie` header with `curl`:

```sh
$ cookie=$(curl -X POST -s -o /dev/null -w '%header{set-cookie}' -d \{\"username\":\"...\",\"password\":\"...\",\"authApp\":\"MariaDB\",\"sessionType\":\"cookie\"\} "https://localhost:8443/mrs/authentication/login")
$ curl -H "Cookie: $cookie" https://localhost:8443/mrs/sakila/actor
```

When you request a bearer token, the JWT is sent in a JSON object in the response body. Extract it with a tool like [`jq`](https://jqlang.org/):

```sh
$ jwt=$(curl -X POST -s -d \{\"username\":\"...\",\"password\":\"...\",\"authApp\":\"MariaDB\",\"sessionType\":\"bearer\"\} "https://localhost:8443/mrs/authentication/login" | jq -r .accessToken)
$ curl -H "Authorization: Bearer $jwt" https://localhost:8443/mrs/sakila/actor
```
