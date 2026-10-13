---
description: >-
  Reference of the commands that the TypeScript client SDK of the MariaDB REST
  Service generates for REST services, views, documents, and routines.
---

# TypeScript Client API Reference

This page describes the Client API of the TypeScript SDK that the MariaDB REST Service (MRS) generates for a REST service. The examples use a REST service `/myService`. For the Python SDK, see the [Python Client API Reference](python-client-api.md).

The MRS TypeScript SDK requires TypeScript 5.3.3 or later.

## getMetadata

`getMetadata` is used to retrieve application-specific metadata attached to an MRS resource (REST Service, Schema and/or Object).

### Return Type

A JSON object containing the application-specific metadata attached to the resource.

### Example

```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

await myService.getMetadata();
await myService.mrsNotes.getMetadata();
await myService.mrsNotes.note.getMetadata();
```

## Service.getAuthApps

Use `getAuthApps` to get a list of available REST authentication apps for the given REST service. A REST service may be linked to several REST auth apps and therefore it is necessary to choose the right one for authentication.

The name of the REST auth apps needs to be passed to the `Service.authenticate` method when performing the authentication process.

## Service.authenticate

Use `authenticate` to authenticate in a given REST service using a given authentication app.

### Options

| Name | Type | Required | Description |
| --- | --- | --- | --- |
| username | string | Yes | Username in the scope of the authentication app. |
| password | string | Yes | Password in the scope of the authentication app. |
| app | string | Yes | Name of the authentication app. |
| vendor | string | No | ID of the vendor of the authentication app. Specifying it avoids an additional round-trip to look up the vendor. |

### Return Type

An `IMrsLoginResult` object. On failure, its `errorMessage` property describes the error.

### Reference

```typescript
async function authenticate (IAuthenticateOptions): Promise<IMrsLoginResult> {
    // ...
}

interface IAuthenticateOptions {
    username: string
    password: string
    app: string
    vendor?: string
}

interface IMrsLoginResult {
    authApp?: string
    jwt?: string
    errorCode?: number
    errorMessage?: string
}
```

### Example

```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

await myService.authenticate({ username: 'foo', password: 'bar', app: 'baz' });
await myService.authenticate({ username: 'foo', password: 'bar', app: 'baz', vendor: "0x30000000000000000000000000000000" });
```

## Service.deauthenticate

`deauthenticate` is used for logging out a user from a given REST service.

### Return Type

Nothing (void).

### Reference

```typescript
async function deauthenticate (): Promise<void> {
    // ...
}

```

### Example

```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

await myService.deauthenticate();
```

## View.create

`create` is used to add a REST Document to a given REST View. The document is represented as a plain TypeScript/JavaScript object or, alternatively, as an instance of a particular class that encapsulates the data required to create a new document. To insert multiple documents, see [createMany](#viewcreatemany).

### Options

| Name | Type | Required | Description |
| --- | --- | --- | --- |
| data  | object | Yes | Object containing the mapping between column names and values for the REST Document to be created. |

### Return Type

A JSON object representing the created REST Documents.

### Reference

```typescript
async function create (args: ICreateOptions<Type>): Promise<Type> {
    // ...
}

interface ICreateOptions<Type> {
    data: Type
}
```

### Example

```typescript
import type { IMyServiceMrsNotesNote } from '/path/to/sdk/myService';
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

// using a plain object
myService.mrsNotes.note.create({ data: { title: 'foo' } });

// using a custom class instance
class Note implements IMyServiceMrsNotesNote {
    // ...
}

const note = new Note();
note.title = 'foo';

myService.mrsNotes.note.create({ data: note });
```

## View.createMany

`createMany` adds one or more REST Documents to a given REST View. The documents are represented as plain TypeScript/JavaScript objects, or alternatively, as instances of a particular class that encapsulates the data required to create them.

### Options

| Name | Type | Required | Description |
| --- | --- | --- | --- |
| data  | object | Yes | Array of objects containing the mapping between column names and values for the REST Documents to be created. |

### Return Type

An array of JSON objects representing the created REST Documents.

### Reference

```typescript
async function createMany (args: ICreateOptions<Type[]>): Promise<Type[]> {
    // ...
}

interface ICreateOptions<Type> {
    data: Type
}
```

### Example

```typescript
import type { IMyServiceMrsNotesNote } from '/path/to/sdk/myService';
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

// using a plain object
myService.mrsNotes.note.createMany({ data: [{ title: 'foo' }, { title: 'bar' }] });

// using a custom class
class Note implements IMyServiceMrsNotesNote {
    // ...
}

const note1 = new Note();
note1.title = 'foo';

const note2 = new Note();
note2.title = 'bar';

myService.mrsNotes.note.createMany({ data: [note1, note2] });
```

## View.find

`find` is used to query the subset of REST documents (that optionally match a given filter) in the first page.

### Options

| Name | Type | Required | Description |
| --- | --- | --- | --- |
| cursor | object | No | Retrieve documents using unique and sequential fields as cursor. |
| orderBy | object | No | Determines the sort order of specific fields. |
| select | object | No | Specifies which properties to include in the returned object. |
| skip  | number | No | How many documents to skip before returning one of the matches. |
| where | object  | No | Filtering conditions that apply to specific fields. |
| take  | number | No | The maximum size of the page. |
| readOwnWrites | boolean | No | Ensures read consistency for a cluster of servers. |

### Return Type

An array of JSON objects representing the first page of REST Documents matching the filter. If there are more matching REST Documents, the array contains an additional `hasMore` truthy property and a `next()` async function that automatically retrieves the subsequent page of REST Documents.

### Reference

```typescript
async function find ({ cursor, orderBy, select, skip, take, where }: IFindManyOptions<Item, Filterable, Cursors>): Promise<PaginatedList<Item>> {
    // ...
}

interface IFindManyOptions<Item, Filterable, Iterable> {
    cursor?: Cursor<Iterable>;
    orderBy?: ColumnOrder<Filterable>;
    select?: BooleanFieldMapSelect<Item> | FieldNameSelect<Item>;
    skip?: number;
    take?: number;
    where?: DataFilter<Filterable>;
    readOwnWrites?: boolean;
}

export interface IExhaustedList<T> extends Array<T> {
    hasMore: false,
}

export interface INotExhaustedList<T> extends Array<T> {
    hasMore: true,
    next(): Promise<PaginatedList<T>>,
}

export type PaginatedList<T> = IExhaustedList<T> | INotExhaustedList<T>;
```

### Example

```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

// get all notes of the first page
await myService.mrsNotes.note.find();
// get the first 3 notes
await myService.mrsNotes.note.find({ take: 3 });
// get notes of then first page where the id is greater than 10
await myService.mrsNotes.note.find({ where: { id: { $gt: 10 } } });

// iterate over the pages
let notes = await myService.mrsNotes.note.find();
if (notes.hasMore) {
    // automatically get the next page (if there is one)
    notes = await notes.next();
}
```

## View.findFirst

`findFirst` is used to query the first REST Document (**in no specific order**) that matches a given optional filter. `findFirstOrThrow` takes the same options, but throws an error instead of returning `undefined` when no document matches.

### Options

| Name | Type | Required | Description |
| --- | --- | --- | --- |
| where | object | No | Filtering conditions that apply to specific fields. |
| select | object | No | Specifies which properties to include in the returned object. |
| skip  | number | No | Specifies how many documents to skip before returning one of the matches. |
| readOwnWrites | boolean | No | Ensures read consistency for a cluster of servers. |

### Return Type

A JSON object representing the first REST document that matches the filter or `undefined` when the document was not found.

### Reference

```typescript
async function findFirst (args?: IFindOptions<Selectable, Filterable>): Promise<Selectable | undefined> {
    // ...
}

export interface IFindOptions<Selectable, Filterable> {
    orderBy?: ColumnOrder<Filterable>;
    select?: BooleanFieldMapSelect<Selectable> | FieldNameSelect<Selectable>;
    skip?: number;
    where?: DataFilter<Filterable>;
    readOwnWrites?: boolean;
}
```

### Example

```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

// get the first note, without any filter
await myService.mrsNotes.note.findFirst();
// get the last note, without any filter
await myService.mrsNotes.note.findFirst({ orderBy: { id: "DESC" } });
// get the second note, without any filter
await myService.mrsNotes.note.findFirst({ skip: 1 });
// get the title and shared fields of the second note
await myService.mrsNotes.note.findFirst({ select: { title: true, shared: true }, skip: 1 });
// get the title and shared fields of the first note
await myService.mrsNotes.note.findFirst({ select: ["title", "shared"] });
// get the first shared note
await myService.mrsNotes.note.findFirst({ where: { shared: true } });
// get the first note whose title includes the string "foo"
await myService.mrsNotes.note.findFirst({ where: { title: { $like: "%foo%" } } });
```

## View.findUnique

`findUnique` is used to query a single, uniquely identified REST Document by:

* Primary key column(s)
* Unique column(s)

If no document was found matching the given `where` condition, `undefined` is returned. To have an exception thrown in this case, see [findUniqueOrThrow](#viewfinduniqueorthrow).

### Options

| Name | Type | Required | Description |
| --- | --- | --- | --- |
| where | object | Yes | Wraps all unique columns so that individual documents can be selected. |
| select | object | No | Specifies which properties to include in the returned object. |
| readOwnWrites | boolean | No | Ensures read consistency for a cluster of servers. |

### Return Type

A JSON object representing the REST document that matches the filter or `undefined` when the document was not found.

### Reference

```typescript
async function findUnique (args?: IFindUniqueOptions<Selectable, Filterable>): Promise<Selectable | undefined> {
    // ...
}

interface IFindUniqueOptions<Selectable, Filterable> {
    select?: BooleanFieldMapSelect<Selectable> | FieldNameSelect<Selectable>;
    where?: DataFilter<Filterable>;
    readOwnWrites?: boolean;
}
```

### Example

```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

// Get the note with id 4.
// using implicit equality
await myService.mrsNotes.note.findUnique({ where: { id: 4 } });
// or using explicit equality
await myService.mrsNotes.note.findUnique({ where: { id: { $eq: 4 } } });
```

## View.findUniqueOrThrow

`findUniqueOrThrow` retrieves a single REST document in the same way as [findUnique](#viewfindunique). However, if the query does not find a document, it throws a `NotFoundError`.

`findUniqueOrThrow` differs from `findUnique` as follows:

* Its return type is non-nullable. For example, `myService.mrsNotes.note.findUnique()` can return a note or `undefined`, but `myService.mrsNotes.note.findUniqueOrThrow()` always returns a note.

## View.delete

`delete` is used to delete the first REST Document that matches a given required filter.

### Options

| Name | Type | Required | Description |
| --- | --- | --- | --- |
| where | object | Yes | Filtering conditions that apply to specific fields. |
| readOwnWrites | boolean | No | Ensures read consistency for a cluster of servers. |

### Return Type

`true` if the document was deleted successfully or `false` otherwise.

### Reference

```typescript
async function delete (args: IDeleteOptions<IMyServiceMrsNotesUserParams>): Promise<IMrsDeleteResult> {
    // ...
}

interface IDeleteOptions<Filterable> {
    where?: DataFilter<Filterable>;
    readOwnWrites?: boolean;
}

interface IMrsDeleteResult {
    itemsDeleted: 1;
}
```

### Example

```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

// delete the first note whose title includes the string "foo"
await myService.mrsNotes.note.delete({ where: { title: { $like: "%foo%" } } });
```

## View.deleteMany

`deleteMany` is used to delete all REST Documents that match a given filter.

### Options

| Name | Type | Required | Description |
| --- | --- | --- | --- |
| where | object | No | Filtering conditions that apply to specific fields. |
| readOwnWrites | boolean | No | Ensures read consistency for a cluster of servers. |

### Return Type

The number of REST Documents that were deleted.

### Reference

```typescript
async function deleteMany (args: IDeleteOptions<IMyServiceMrsNotesUserParams>): Promise<number> {
    // ...
}

interface IDeleteOptions<Filterable> {
    where?: DataFilter<Filterable>;
    readOwnWrites: boolean;
}
```

### Example

```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

// delete all notes whose title includes the string "foo"
await myService.mrsNotes.note.deleteMany({ where: { title: { $like: "%foo%" } } });
// delete all shared notes
await myService.mrsNotes.note.deleteMany({ where: { shared: true } });
```

## View.update

`update` is used to update a REST Document with a given identifier or primary key.

### Options

| Name | Type | Required | Description |
| --- | --- | --- | --- |
| data | object | Yes | Set of fields and corresponding values to update. |

### Return Type

A JSON object representing the up-to-date REST document.

### Reference

```typescript
async function update (args: IUpdateOptions<UpdatableFields>): Promise<Data> {
    // ...
}

type IUpdateOptions<Type> = ICreateOptions<Type>;
```

### Example

```typescript
import type { IMyServiceMrsNotesNote } from '/path/to/sdk/myService';
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

// update the note with id is 1 using a plain object
await myService.mrsNotes.note.update({ data: { id: 1, title: 'bar' } });

// using a custom class instance
class Note implements IMyServiceMrsNotesNote {
    // ...
}

const note = new Note();
note.id = 1
note.shared = false;

// update the note with id 1
await myService.mrsNotes.note.update({ data: note });
```

## View.updateMany

`updateMany` is used to update all REST Documents with matching identifiers or primary keys.

### Options

| Name | Type | Required | Description |
| --- | --- | --- | --- |
| data | object | Yes | Set of fields and corresponding values to update. |

### Return Type

An array of JSON objects representing the up-to-date REST documents.

### Reference

```typescript
async function updateMany (args: IUpdateOptions<UpdatableFields[]>): Promise<Data[]> {
    // ...
}

type IUpdateOptions<Type> = ICreateOptions<Type>;
```

### Example

```typescript
import type { IMyServiceMrsNotesNote } from '/path/to/sdk/myService';
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

// update the notes with id 1 and 2 using a plain object
await myService.mrsNotes.note.updateMany({ data: [{ id: 1, title: 'bar' }, { id: 2, title: 'bar' }] });

// using a custom class instance
class Note implements IMyServiceMrsNotesNote {
    // ...
}

const note1 = new Note();
note1.id = 1;
note1.shared = false;

const note2 = new Note();
note2.id = 2;
note2.shared = false;

// update the notes with id 1 and 2
await myService.mrsNotes.note.updateMany({ data: [note1, note2] });
```

## Document.update

`update` is used to update a given REST document by committing the set of updates performed locally on the corresponding instance in the application.

{% hint style="info" %}
This function is only available if the REST view enables the `UPDATE` CRUD operation and specifies one or more identifier fields.
{% endhint %}

### Reference

```typescript
async function update(): Promise<IMyServiceSakilaActor> {
    // ...
}

interface IMyServiceSakilaActor {
    readonly actorId?: number;
    firstName?: string;
    lastName?: string;
    lastUpdate?: string;
}
```

### Example

```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

const actor = await myService.sakila.actor.findFirst();
if (actor) {
    actor.lastName = "FOO";
    const modifiedActor = await actor.update();
    console.log(modifiedActor.lastName); // FOO
}
```

## Document.delete

`delete` is used to delete a given REST document represented by a corresponding instance in the application.

{% hint style="info" %}
This function is only available if the REST view enables the `DELETE` CRUD operation and specifies one or more identifier fields.
{% endhint %}

### Return Type

`true` if the document was deleted successfully or `false` otherwise.

### Reference

```typescript
async function delete(): Promise<boolean> {
    // ...
}
```

### Example

```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

const actor = await myService.sakila.actor.findFirst();
if (actor) {
    if (await actor.delete()) {
        console.log(`Actor ${actor.actorId} was deleted.`)
    } else {
        console.log(`Actor ${actor.actorId} was not deleted.`)
    }
}
```

## Function.call

`call` is used to execute a REST routine (`FUNCTION` or `PROCEDURE`). The first parameter of the command is an `object` containing the set of `IN`/`INOUT` parameters (and corresponding values) as specified by the database routine.

### Return Type

In the case of a `FUNCTION`, the value returned by that function. In the case of a `PROCEDURE`, a JSON object containing the result produced by the procedure (including `OUT`/`INOUT` parameters and result sets).

### Reference

```typescript
async function call (noteUpdateParams?: IMyServiceMrsNotesNoteUpdateParams): Promise<IMrsProcedureResult<IMyServiceMrsNotesNoteUpdateParamsOut, IMyServiceMrsNotesNoteUpdateResultSet>> {
    // ...
}

interface IMyServiceMrsNotesNoteUpdateParams {
    tags?: JsonValue;
    lockedDown?: boolean;
    noteId?: number;
    title?: string;
    content?: string;
    pinned?: boolean;
    userId?: string;
}

type IMyServiceMrsNotesNoteUpdateParamsOut = never;

type IMyServiceMrsNotesNoteUpdateResultSet = JsonObject;

interface IMrsProcedureResult<OutParams, ResultSet> {
    outParameters?: OutParams;
    resultSets: ResultSet[];
}
```

### Example

```typescript
import { MyService } from './myService.mrs.sdk/myService';

const myService = new MyService();

// update the title of a note with a given id
await myService.mrsNotes.noteUpdate.call({ noteId: note.id, title: "hello world" });
```

## Procedure.call

`call` is used to execute a REST routine (`FUNCTION` or `PROCEDURE`). See [Function.call](#functioncall) for more details.
