// Assumptions: validateMembers exists
var mysql = require('mysql');

function astContainsRule(ast, rule) {
    return JSON.stringify(ast).includes('"rule":"' + rule + '"');
}

function astContainsSymbol(ast, symbol) {
    return JSON.stringify(ast).includes('"symbol":"' + symbol + '"');
}

// The tests assume the next variables have been put in place
// on the JS Context
// __uri: <user>@<host>
// __host: <host>
// __port: <port>
// __user: <user>
// __uripwd: <uri>:<pwd>@<host>

//@<> mysql module: exports
validateMembers(mysql, [
    'getClassicSession',
    'ErrorCode',
    'Type',
    'getSession',
    'help',
    'parseStatementAst',
    'tokenizeStatement',
    'quoteIdentifier',
    'splitScript',
    'unquoteIdentifier',
    'makeAccount',
    'splitAccount'
])

//@# getClassicSession errors
mysql.getClassicSession()
mysql.getClassicSession(1, 2, 3)
mysql.getClassicSession(["bla"])
mysql.getClassicSession("some@uri", 25)

//@# getSession errors
mysql.getSession()
mysql.getSession(1, 2, 3)
mysql.getSession(["bla"])
mysql.getSession("some@uri", 25)

//@<> ErrorCode
EXPECT_EQ(1045, mysql.ErrorCode.ER_ACCESS_DENIED_ERROR)

//@# parseStatementAst errors
mysql.parseStatementAst(1);
mysql.parseStatementAst({});

//@ parseStatementAst
mysql.parseStatementAst("this is not valid sql");
mysql.parseStatementAst("");
mysql.parseStatementAst("SELECT");

//@ tokenizeStatement
mysql.tokenizeStatement("select * from /* comment */ foo.bar /* `\"'` '*/")

//@# tokenizeStatement errors
mysql.tokenizeStatement("/*")
mysql.tokenizeStatement("'")
mysql.tokenizeStatement("`")
mysql.tokenizeStatement('"')
mysql.tokenizeStatement('\\')

//@<> BUG#37018247 - double quotes follow sql_mode token classes
EXPECT_NO_THROWS(function () { mysql.parseStatementAst('SELECT "1"'); });
EXPECT_EQ(true, astContainsRule(mysql.parseStatementAst('SELECT "1"'), 'pureIdentifier'));
var doubleQuotedLiteralAst = mysql.parseStatementAst('SELECT "1"', {ansiQuotes: false});
EXPECT_EQ(true, astContainsRule(doubleQuotedLiteralAst, 'textStringLiteral'));
EXPECT_EQ(true, astContainsSymbol(doubleQuotedLiteralAst, 'SINGLE_QUOTED_TEXT'));
EXPECT_EQ(false, astContainsSymbol(doubleQuotedLiteralAst, 'DOUBLE_QUOTED_TEXT'));
var doubleQuotedIdentifierAst = mysql.parseStatementAst('SELECT "quoted"', {ansiQuotes: true});
EXPECT_EQ(true, astContainsRule(doubleQuotedIdentifierAst, 'pureIdentifier'));
EXPECT_EQ(true, astContainsSymbol(doubleQuotedIdentifierAst, 'BACK_TICK_QUOTED_ID'));
EXPECT_THROWS_LIKE(function () { mysql.parseStatementAst('SELECT "a" FROM "t"', {ansiQuotes: false}); }, /".*"/);

EXPECT_EQ(1, mysql.tokenizeStatement('SELECT "a"').filter(function (token) {
    return token.type === 'BACK_TICK_QUOTED_ID';
}).length);
EXPECT_EQ(1, mysql.tokenizeStatement('SELECT "a"', {ansiQuotes: false}).filter(function (token) {
    return token.type === 'SINGLE_QUOTED_TEXT';
}).length);
EXPECT_EQ(1, mysql.tokenizeStatement('SELECT "a"', {ansiQuotes: true}).filter(function (token) {
    return token.type === 'BACK_TICK_QUOTED_ID';
}).length);
EXPECT_EQ(1, mysql.tokenizeStatement("SELECT 'a\\''", {noBackslashEscapes: false}).filter(function (token) {
    return token.type === 'SINGLE_QUOTED_TEXT';
}).length);
EXPECT_THROWS_LIKE(function () { mysql.tokenizeStatement("SELECT 'a\\''", {noBackslashEscapes: true}); }, /token recognition error/);

EXPECT_EQ(2, mysql.splitScript("SELECT 'a\\''; SELECT 2", {noBackslashEscapes: false}).length);
EXPECT_EQ(1, mysql.splitScript("SELECT 'a\\''; SELECT 2", {noBackslashEscapes: true}).length);

//@ splitScript
mysql.splitScript("select 1")
mysql.splitScript("select 1; select 2;")
mysql.splitScript("delimiter $$\nselect 1$$select 2;select3$$")
mysql.splitScript("DELIMITER A\nSELECT 1 A SELECT 2 A SELECT 3; SELECT 4")

//@# splitScript errors
mysql.splitScript(1)
mysql.splitScript(["select"])

//<>@ makeAccount
mysql.makeAccount("user", "localhost")
EXPECT_OUTPUT_CONTAINS("'user'@'localhost'")
mysql.makeAccount("oth'er", "%")
EXPECT_OUTPUT_CONTAINS("'oth\\'er'@'%'")

//<>@ splitAccount
mysql.splitAccount("user@localhost")
EXPECT_OUTPUT_CONTAINS("\"user\": \"user\"")
EXPECT_OUTPUT_CONTAINS("\"host\": \"localhost\"")
mysql.splitAccount("oth'er@%")
EXPECT_OUTPUT_CONTAINS("\"user\": \"oth'er\"")
EXPECT_OUTPUT_CONTAINS("\"host\": \"%\"")
mysql.splitAccount("'name'@'somehost'")
EXPECT_OUTPUT_CONTAINS("\"user\": \"name\"")
EXPECT_OUTPUT_CONTAINS("\"host\": \"somehost\"")
