---
description: >-
  Understand the MariaDB roles that administer and operate the MariaDB REST
  Service, the authentication methods for REST users, and the REST roles and
  privileges that authorize access to REST endpoints.
---

# Authentication and Authorization

## Overview

As an HTTP REST service, the MariaDB REST Service (MRS) performs its own authentication and authorization checks, separately from the MariaDB Server.

Anyone or anything that accesses an MRS endpoint first authenticates with it as a specific user account. That user also needs the privileges to access the object and to execute the HTTP method, for example GET or POST.

MRS defines five types of users, according to the activities they perform in an MRS deployment. They map to five roles created during [MRS configuration](configuring-mrs.md#granting-users-access-to-mrs), which you grant in any combination to one or more MariaDB accounts.

All roles have the minimal set of privileges they need, mostly restricted to the internal MRS metadata tables. By default, they have no access to other schemas or tables. Some roles need varying levels of access to user schemas, tables, and other database objects for their purpose.

### MRS Administrative Users

You perform administrative tasks, like configuration and creating and managing endpoints, in MariaDB Shell. The operations you can perform depend on the MariaDB account that MariaDB Shell is connected with, that is, the user name of the DB connection in MariaDB Shell for VS Code or of the connection given on the `mariadb-shell` command line.

{% hint style="info" %}
A `root` account or any other account with full privileges on the MariaDB Server also has full privileges on MRS. Manage MRS with a dedicated MariaDB account with minimal privileges instead.
{% endhint %}

#### Service Administrator (`mysql_rest_service_admin`)

A service administrator can:

* Add, manage, and remove REST services.
* Add, manage, and remove REST schemas.
* Add, manage, and remove endpoints for tables, views, and routines in REST schemas.
* Add, manage, and remove REST authentication apps.
* Add, manage, and remove REST users and roles.
* Add, manage, and remove content sets and files, the static files that are served over HTTP.
* Manage and monitor the MariaDB REST Daemon instances.
* View the MRS audit log.

Service administrators need the `mysql_rest_service_admin` role.

#### Schema Administrator (`mysql_rest_service_schema_admin`)

The main task of a schema administrator is to create REST schemas, so that the objects of the schema can be published as endpoints of a REST service. Schema administrators need access to both the MRS metadata tables and the user schemas.

A schema administrator can:

* Add, manage, and remove REST schemas.
* Add, manage, and remove endpoints for tables, views, and routines in REST schemas.
* Add, manage, and remove content sets and files.
* Monitor the MariaDB REST Daemon instances.
* View the MRS audit log.

Schema administrators need the `mysql_rest_service_schema_admin` role. In addition, they need to see the objects they create endpoints for, and to hold all relevant privileges on them `WITH GRANT OPTION`, for example `SELECT`, `INSERT`, `UPDATE`, and `DELETE` on tables that are published as updatable. MariaDB Shell grants these privileges to the `mysql_rest_service_data_provider` role, so that the MariaDB REST Daemon can do what the endpoint configuration requires.

#### Developer (`mysql_rest_service_dev`)

Developers have mostly the same privileges as schema administrators, but they can't add REST schemas to a REST service.

A developer can:

* Use existing REST schemas.
* Add, manage, and remove endpoints for tables, views, and routines in REST schemas.
* Add, manage, and remove content sets and files.
* Monitor the MariaDB REST Daemon instances.
* View the MRS audit log.

Developers need the `mysql_rest_service_dev` role. In addition, they need to hold all relevant privileges `WITH GRANT OPTION` on the objects they create endpoints for, for example `SELECT`, `INSERT`, `UPDATE`, and `DELETE` on tables that are published as updatable. MariaDB Shell grants these privileges to the `mysql_rest_service_data_provider` role, so that the MariaDB REST Daemon can do what the endpoint configuration requires.

### MRS Service Accounts

The MariaDB REST Daemon connects to the MariaDB Server with accounts that have the following two roles. See [MariaDB Accounts for the MariaDB REST Daemon](configuring-mrs.md#mariadb-accounts-for-the-mariadb-rest-daemon).

#### Data Access (`mysql_rest_service_data_provider`)

The MariaDB REST Daemon uses this role to run the SQL that serves the HTTP REST requests on behalf of REST users. The role needs privileges on all database objects that are exposed as REST endpoints.

MariaDB Shell manages the privileges of this role as you add database objects as REST endpoints.

{% hint style="warning" %}
MRS checks the access of REST users at the level of the REST endpoints, but all REST requests run through the same MariaDB account, whichever REST user sent them. Take care when you expose views and stored procedures, so that you don't give unintended users access to objects.
{% endhint %}

#### Metadata Access (`mysql_rest_service_meta_provider`)

The MariaDB REST Daemon uses this role to query the MRS metadata for the endpoint configuration, REST user account information, and similar data. The role has access to the internal metadata tables only.

## Authentication Management

MRS supports the following authentication methods. You set up a method by creating a REST authentication app with [`CREATE REST AUTH APP`](../rest-sql-reference/rest-authentication.md#create-rest-auth-app) and assigning it to the REST service. To list the vendors, run [`SHOW REST AUTH VENDORS`](../rest-sql-reference/rest-authentication.md#show-rest-auth-vendors).

### MRS REST Service Specific Authentication

MRS authenticates users against REST user accounts specific to the REST service, created with [`CREATE REST USER`](../rest-sql-reference/rest-users-and-roles.md#create-rest-user). Applications use SCRAM (Salted Challenge Response Authentication Mechanism) to authenticate a user securely. The vendor of the REST authentication app is `MRS`:

```sql
CREATE REST AUTH APP "MyAuthApp" VENDOR MRS;
ALTER REST SERVICE /myService ADD AUTH APP "MyAuthApp";
CREATE REST USER "jane"@"MyAuthApp" IDENTIFIED BY "********";
```

### MariaDB Internal Authentication

MRS authenticates users against the accounts of the MariaDB Server, with the vendor `MySQL Internal`. Applications send the credentials, user name and password, in clear text as part of a JSON request payload to the MariaDB REST Daemon.

Use this method for HTTPS-only REST services, and preferably for applications that are not exposed publicly. The MariaDB accounts need the `mysql_rest_service_user` role, and they access the data with their own privileges.

### OAuth2 Authentication

MRS supports OAuth2 services of third-party vendors, for example sign-in with Facebook or Google. To authenticate a REST service against such a vendor, register as a developer with the vendor and create an authentication app there. Then create a REST authentication app for the vendor with the settings of that app, like its app ID and app secret.

#### Configuring the Redirection URL of a REST Service

The OAuth2 vendor sends the user back to MRS after the login. Register the redirection URL in the vendor's authentication app in the following format:

```text
https://<daemon-address>/<rest-service>/authentication/login?authApp=<authAppName>&sessionType=<bearer | cookie>
```

For example: `https://rest.example.com/myService/authentication/login?authApp=MyOAuth2App&sessionType=cookie`.

After you create the REST authentication app, configure the redirection URL of the REST service. It tells the MariaDB REST Daemon where to send the user after the authentication against the OAuth2 server has completed. Set it with the [`ALTER REST SERVICE`](../rest-sql-reference/rest-services.md#alter-rest-service) statement, see [REST Service Authentication Settings](../rest-sql-reference/rest-services.md#rest-service-authentication-settings), or on the **Authentication** tab of the REST service dialog in MariaDB Shell for VS Code.

## Authorization Management

Access to a REST resource can have several levels of restriction:

* Public access: no authorization is needed to access the REST resource and its data.
* Full access: after authentication, the user has full access to all data of the REST resource.
* Limited access: after authentication, the user has access to a subset of the data of the REST resource.

MRS has built-in support for several authorization models. They define which data of a REST resource the end users can see and manipulate:

* User-ownership based: users see their own data.
* Privilege based, managed with roles.
* User-hierarchy based.
* Group based.
* Group-hierarchy based.

If the use case of your project matches one of these models, you don't need to implement a custom authorization.

From an endpoint's perspective, you control access to REST resources at the following levels:

* Service
* Schema
* Object

For example, a user with read access to a REST schema has read access to all objects in that schema of the REST service.

You can grant the CREATE, READ, UPDATE, and DELETE privileges at each of these levels with [`GRANT REST`](../rest-sql-reference/rest-users-and-roles.md#grant-rest).

### MRS Roles

An MRS role holds a set of privileges on REST endpoints, which you grant as a whole to individual REST users of a service. Roles can be organized hierarchically, or extended into new roles with additional privileges.

For example, a simple blog application with the endpoint `/myService/blog/post` could have three roles:

* `reader`, who can only read posts.
* `poster`, who can also create and update posts.
* `editor`, who has the privileges of a `poster` and can also delete posts.

The following script creates the three roles with [`CREATE REST ROLE`](../rest-sql-reference/rest-users-and-roles.md#create-rest-role), grants them their privileges, and grants each of them to a different REST user with [`GRANT REST ROLE`](../rest-sql-reference/rest-users-and-roles.md#grant-rest-role):

```sql
-- Create the database schema blog with the table post
CREATE SCHEMA IF NOT EXISTS blog;
CREATE TABLE IF NOT EXISTS blog.post(id INT PRIMARY KEY AUTO_INCREMENT, message TEXT);

CREATE REST SERVICE /myTestService;
USE REST SERVICE /myTestService;

CREATE REST SCHEMA /blog FROM blog;
CREATE REST VIEW /post ON SCHEMA /blog AS blog.post;

CREATE REST ROLE "reader";
GRANT REST READ ON SCHEMA /blog OBJECT /post TO "reader";

CREATE REST ROLE "poster" EXTENDS "reader";
GRANT REST CREATE, UPDATE ON SCHEMA /blog OBJECT /post TO "poster";

CREATE REST ROLE "editor" EXTENDS "poster";
GRANT REST DELETE ON SCHEMA /blog OBJECT /post TO "editor";

SHOW REST ROLES;

CREATE REST AUTH APP "TestAuthApp" VENDOR MRS;

CREATE REST USER "ulf"@"TestAuthApp" IDENTIFIED BY "********";
GRANT REST ROLE "reader" TO "ulf"@"TestAuthApp";

CREATE REST USER "alfredo"@"TestAuthApp" IDENTIFIED BY "********";
GRANT REST ROLE "poster" TO "alfredo"@"TestAuthApp";

CREATE REST USER "mike"@"TestAuthApp" IDENTIFIED BY "********";
GRANT REST ROLE "editor" TO "mike"@"TestAuthApp";

-- The three users can now log in with their passwords through MRS authentication
```

By default, a role belongs to a REST service. You name the service in the `CREATE REST ROLE` statement, and without it, the role is created in the current REST service, set with [`USE REST SERVICE`](../rest-sql-reference/rest-metadata.md#use-rest). Role names need to be unique within a service only:

```sql
CREATE REST ROLE "myrole" ON SERVICE /myOtherService;

-- The role is created in /myTestService, the current REST service
CREATE REST ROLE "myrole";

SHOW CREATE REST ROLE "myrole" ON SERVICE /myTestService;
SHOW CREATE REST ROLE "myrole" ON SERVICE /myOtherService;
```

To create a role that can be used in any service, add the `ON ANY SERVICE` clause:

```sql
CREATE REST ROLE "globalRole" ON ANY SERVICE;
```
