/*
 * Copyright (c) 2026, MariaDB plc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms,
 * as designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 *
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef MODULES_MRS_CORE_MRS_LEXER_H_
#define MODULES_MRS_CORE_MRS_LEXER_H_

// The hand-written lexer of the MRS SQL extension. It follows the MariaDB
// server's lexing rules (sql_lex.cc) for the token classes the REST grammar
// needs: identifiers, quoted identifiers and strings (honouring the
// ANSI_QUOTES and NO_BACKSLASH_ESCAPES SQL modes), numbers, comments, and the
// REST specific request paths (`/a/b`) and @-annotations of data mappings.

#include <cstddef>
#include <string>
#include <string_view>

namespace mrs {

// The SQL modes that change how a statement is lexed.
struct Sql_mode {
  bool ansi_quotes = false;
  bool no_backslash_escapes = false;

  // Parses a comma separated @@sql_mode value.
  static Sql_mode from_string(std::string_view sql_mode);
};

// Keywords of the grammar. The lexer recognises them case-insensitively and
// the parser driver maps them to the bison token of the same name. The names
// carry the _SYMBOL suffix of the tokens so that none of them (NULL) clashes
// with a macro.
#define MRS_KEYWORD_LIST(X)                     \
  X(CREATE_SYMBOL, "CREATE")                           \
  X(OR_SYMBOL, "OR")                                   \
  X(REPLACE_SYMBOL, "REPLACE")                         \
  X(ALTER_SYMBOL, "ALTER")                             \
  X(SHOW_SYMBOL, "SHOW")                               \
  X(STATUS_SYMBOL, "STATUS")                           \
  X(NEW_SYMBOL, "NEW")                                 \
  X(ON_SYMBOL, "ON")                                   \
  X(FROM_SYMBOL, "FROM")                               \
  X(IN_SYMBOL, "IN")                                   \
  X(DATABASES_SYMBOL, "DATABASES")                     \
  X(DATABASE_SYMBOL, "DATABASE")                       \
  X(JSON_SYMBOL, "JSON")                               \
  X(VIEW_SYMBOL, "VIEW")                               \
  X(PROCEDURE_SYMBOL, "PROCEDURE")                     \
  X(FUNCTION_SYMBOL, "FUNCTION")                       \
  X(DROP_SYMBOL, "DROP")                               \
  X(USE_SYMBOL, "USE")                                 \
  X(AS_SYMBOL, "AS")                                   \
  X(FILTER_SYMBOL, "FILTER")                           \
  X(AUTHENTICATION_SYMBOL, "AUTHENTICATION")           \
  X(PATH_SYMBOL, "PATH")                               \
  X(VALIDATION_SYMBOL, "VALIDATION")                   \
  X(DEFAULT_SYMBOL, "DEFAULT")                         \
  X(USER_SYMBOL, "USER")                               \
  X(OPTIONS_SYMBOL, "OPTIONS")                         \
  X(IF_SYMBOL, "IF")                                   \
  X(NOT_SYMBOL, "NOT")                                 \
  X(EXISTS_SYMBOL, "EXISTS")                           \
  X(PAGE_SYMBOL, "PAGE")                               \
  X(HOST_SYMBOL, "HOST")                               \
  X(TYPE_SYMBOL, "TYPE")                               \
  X(FORMAT_SYMBOL, "FORMAT")                           \
  X(FORCE_SYMBOL, "FORCE")                             \
  X(UPDATE_SYMBOL, "UPDATE")                           \
  X(NULL_SYMBOL, "NULL")                               \
  X(TRUE_SYMBOL, "TRUE")                               \
  X(FALSE_SYMBOL, "FALSE")                             \
  X(SET_SYMBOL, "SET")                                 \
  X(IDENTIFIED_SYMBOL, "IDENTIFIED")                   \
  X(BY_SYMBOL, "BY")                                   \
  X(ROLE_SYMBOL, "ROLE")                               \
  X(TO_SYMBOL, "TO")                                   \
  X(IGNORE_SYMBOL, "IGNORE")                           \
  X(CLONE_SYMBOL, "CLONE")                             \
  X(FILE_SYMBOL, "FILE")                               \
  X(FILES_SYMBOL, "FILES")                             \
  X(BINARY_SYMBOL, "BINARY")                           \
  X(DATA_SYMBOL, "DATA")                               \
  X(LOAD_SYMBOL, "LOAD")                               \
  X(GRANT_SYMBOL, "GRANT")                             \
  X(READ_SYMBOL, "READ")                               \
  X(DELETE_SYMBOL, "DELETE")                           \
  X(GROUP_SYMBOL, "GROUP")                             \
  X(REVOKE_SYMBOL, "REVOKE")                           \
  X(ACCOUNT_SYMBOL, "ACCOUNT")                         \
  X(LOCK_SYMBOL, "LOCK")                               \
  X(UNLOCK_SYMBOL, "UNLOCK")                           \
  X(GRANTS_SYMBOL, "GRANTS")                           \
  X(FOR_SYMBOL, "FOR")                                 \
  X(LEVEL_SYMBOL, "LEVEL")                             \
  X(ANY_SYMBOL, "ANY")                                 \
  X(CLIENT_SYMBOL, "CLIENT")                           \
  X(URL_SYMBOL, "URL")                                 \
  X(NAME_SYMBOL, "NAME")                               \
  X(DO_SYMBOL, "DO")                                   \
  X(ALL_SYMBOL, "ALL")                                 \
  X(PARAMETERS_SYMBOL, "PARAMETERS")                   \
  X(ADD_SYMBOL, "ADD")                                 \
  X(REMOVE_SYMBOL, "REMOVE")                           \
  X(MERGE_SYMBOL, "MERGE")                             \
  X(COMMENT_SYMBOL, "COMMENT")                         \
  X(DYNAMIC_SYMBOL, "DYNAMIC")                         \
  X(AND_SYMBOL, "AND")                                 \
  X(SETS_SYMBOL, "SETS")                               \
  X(CONFIGURE_SYMBOL, "CONFIGURE")                     \
  X(REST_SYMBOL, "REST")                               \
  X(METADATA_SYMBOL, "METADATA")                       \
  X(SERVICES_SYMBOL, "SERVICES")                       \
  X(SERVICE_SYMBOL, "SERVICE")                         \
  X(VIEWS_SYMBOL, "VIEWS")                             \
  X(PROCEDURES_SYMBOL, "PROCEDURES")                   \
  X(FUNCTIONS_SYMBOL, "FUNCTIONS")                     \
  X(RESULT_SYMBOL, "RESULT")                           \
  X(ENABLED_SYMBOL, "ENABLED")                         \
  X(PUBLISHED_SYMBOL, "PUBLISHED")                     \
  X(DISABLED_SYMBOL, "DISABLED")                       \
  X(PRIVATE_SYMBOL, "PRIVATE")                         \
  X(UNPUBLISHED_SYMBOL, "UNPUBLISHED")                 \
  X(PROTOCOL_SYMBOL, "PROTOCOL")                       \
  X(HTTP_SYMBOL, "HTTP")                               \
  X(HTTPS_SYMBOL, "HTTPS")                             \
  X(REQUEST_SYMBOL, "REQUEST")                         \
  X(REDIRECTION_SYMBOL, "REDIRECTION")                 \
  X(MANAGEMENT_SYMBOL, "MANAGEMENT")                   \
  X(AVAILABLE_SYMBOL, "AVAILABLE")                     \
  X(REQUIRED_SYMBOL, "REQUIRED")                       \
  X(ITEMS_SYMBOL, "ITEMS")                             \
  X(PER_SYMBOL, "PER")                                 \
  X(CONTENT_SYMBOL, "CONTENT")                         \
  X(MEDIA_SYMBOL, "MEDIA")                             \
  X(AUTODETECT_SYMBOL, "AUTODETECT")                   \
  X(FEED_SYMBOL, "FEED")                               \
  X(ITEM_SYMBOL, "ITEM")                               \
  X(AUTH_SYMBOL, "AUTH")                               \
  X(APPS_SYMBOL, "APPS")                               \
  X(APP_SYMBOL, "APP")                                 \
  X(ID_SYMBOL, "ID")                                   \
  X(SECRET_SYMBOL, "SECRET")                           \
  X(VENDOR_SYMBOL, "VENDOR")                           \
  X(MRS_SYMBOL, "MRS")                                 \
  X(MYSQL_SYMBOL, "MYSQL")                             \
  X(USERS_SYMBOL, "USERS")                             \
  X(ALLOW_SYMBOL, "ALLOW")                             \
  X(REGISTER_SYMBOL, "REGISTER")                       \
  X(CLASS_SYMBOL, "CLASS")                             \
  X(DEVELOPMENT_SYMBOL, "DEVELOPMENT")                 \
  X(SCRIPTS_SYMBOL, "SCRIPTS")                         \
  X(MAPPING_SYMBOL, "MAPPING")                         \
  X(TYPESCRIPT_SYMBOL, "TYPESCRIPT")                   \
  X(ROLES_SYMBOL, "ROLES")                             \
  X(EXTENDS_SYMBOL, "EXTENDS")                         \
  X(OBJECT_SYMBOL, "OBJECT")                           \
  X(HIERARCHY_SYMBOL, "HIERARCHY")                     \
  X(INCLUDE_SYMBOL, "INCLUDE")                         \
  X(INCLUDING_SYMBOL, "INCLUDING")                     \
  X(ENDPOINTS_SYMBOL, "ENDPOINTS")                     \
  X(OBJECTS_SYMBOL, "OBJECTS")                         \
  X(STATIC_SYMBOL, "STATIC")                           \
  X(VENDORS_SYMBOL, "VENDORS")                       \
  X(TABLE_SYMBOL, "TABLE")                           \
  X(COLUMNS_SYMBOL, "COLUMNS")                       \
  X(DAEMON_SYMBOL, "DAEMON")                         \
  X(DAEMONS_SYMBOL, "DAEMONS")

// The @-annotations of data mapping definitions (`@INSERT`, `@KEY`, ...).
#define MRS_ANNOTATION_LIST(X)      \
  X(AT_INOUT_SYMBOL, "INOUT")              \
  X(AT_IN_SYMBOL, "IN")                    \
  X(AT_OUT_SYMBOL, "OUT")                  \
  X(AT_CHECK_SYMBOL, "CHECK")              \
  X(AT_NOCHECK_SYMBOL, "NOCHECK")          \
  X(AT_NOUPDATE_SYMBOL, "NOUPDATE")        \
  X(AT_SORTABLE_SYMBOL, "SORTABLE")        \
  X(AT_NOFILTERING_SYMBOL, "NOFILTERING")  \
  X(AT_ROWOWNERSHIP_SYMBOL, "ROWOWNERSHIP") \
  X(AT_UNNEST_SYMBOL, "UNNEST")            \
  X(AT_DATATYPE_SYMBOL, "DATATYPE")        \
  X(AT_SELECT_SYMBOL, "SELECT")            \
  X(AT_NOSELECT_SYMBOL, "NOSELECT")        \
  X(AT_INSERT_SYMBOL, "INSERT")            \
  X(AT_NOINSERT_SYMBOL, "NOINSERT")        \
  X(AT_UPDATE_SYMBOL, "UPDATE")            \
  X(AT_DELETE_SYMBOL, "DELETE")            \
  X(AT_NODELETE_SYMBOL, "NODELETE")        \
  X(AT_KEY_SYMBOL, "KEY")

// Single character tokens.
#define MRS_PUNCTUATION_LIST(X)    \
  X(EQUAL_OPERATOR, '=')           \
  X(PLUS_OPERATOR, '+')            \
  X(MINUS_OPERATOR, '-')           \
  X(MULT_OPERATOR, '*')            \
  X(DIV_OPERATOR, '/')             \
  X(DOT_SYMBOL, '.')               \
  X(COMMA_SYMBOL, ',')             \
  X(SEMICOLON_SYMBOL, ';')         \
  X(COLON_SYMBOL, ':')             \
  X(OPEN_PAR_SYMBOL, '(')          \
  X(CLOSE_PAR_SYMBOL, ')')         \
  X(OPEN_CURLY_SYMBOL, '{')        \
  X(CLOSE_CURLY_SYMBOL, '}')       \
  X(OPEN_SQUARE_SYMBOL, '[')       \
  X(CLOSE_SQUARE_SYMBOL, ']')      \
  X(AT_SIGN_SYMBOL, '@')

struct Token {
  enum class Kind {
    end_of_input,
    invalid,  // a character the grammar has no use for
    identifier,
    back_tick_quoted_id,
    single_quoted_text,
    double_quoted_text,
    int_number,
    decimal_number,
    float_number,
    rest_request_path,
    keyword,
    annotation,
    punctuation,
  };

  enum class Keyword {
#define MRS_KEYWORD_ENUM(name, text) name,
    MRS_KEYWORD_LIST(MRS_KEYWORD_ENUM)
#undef MRS_KEYWORD_ENUM
  };

  enum class Annotation {
#define MRS_ANNOTATION_ENUM(name, text) name,
    MRS_ANNOTATION_LIST(MRS_ANNOTATION_ENUM)
#undef MRS_ANNOTATION_ENUM
  };

  enum class Punctuation {
#define MRS_PUNCTUATION_ENUM(name, ch) name,
    MRS_PUNCTUATION_LIST(MRS_PUNCTUATION_ENUM)
#undef MRS_PUNCTUATION_ENUM
  };

  Kind kind = Kind::end_of_input;
  Keyword keyword = Keyword::CREATE_SYMBOL;
  Annotation annotation = Annotation::AT_IN_SYMBOL;
  Punctuation punctuation = Punctuation::EQUAL_OPERATOR;

  // The value of the token: the unquoted and unescaped text of quoted tokens,
  // the source text otherwise.
  std::string text;
  // The token as written in the source.
  std::string_view raw;

  // 1-based line and 0-based column of the first character, as reported in
  // syntax errors.
  int line = 1;
  int column = 0;
  size_t offset = 0;
};

class Lexer {
 public:
  Lexer(std::string_view input, const Sql_mode &sql_mode = {});

  // Returns the next token; the end_of_input token is returned repeatedly
  // once the input is exhausted.
  Token next();

  const Sql_mode &sql_mode() const { return m_sql_mode; }

  // The keyword text of a keyword, for messages.
  static std::string_view keyword_text(Token::Keyword keyword);

 private:
  bool at_end() const { return m_pos >= m_input.size(); }
  char peek(size_t ahead = 0) const {
    return m_pos + ahead < m_input.size() ? m_input[m_pos + ahead] : '\0';
  }
  void advance(size_t count = 1);

  void skip_whitespace_and_comments();
  Token make_token(Token::Kind kind, size_t start) const;

  Token lex_number(size_t start);
  Token lex_identifier_or_keyword(size_t start);
  Token lex_quoted(size_t start, char quote);
  Token lex_request_path(size_t start);
  Token lex_at(size_t start);

  std::string unescape(std::string_view body, char quote) const;

  std::string_view m_input;
  Sql_mode m_sql_mode;
  size_t m_pos = 0;
  int m_line = 1;
  int m_column = 0;
};

// Helpers shared by the lexer and the executor.
bool is_identifier_start(char c);
bool is_identifier_char(char c);

}  // namespace mrs

#endif  // MODULES_MRS_CORE_MRS_LEXER_H_
