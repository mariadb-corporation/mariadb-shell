/*
 * Copyright (c) 2019, 2024, Oracle and/or its affiliates.
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

#include "modules/reports/utils.h"

#include <algorithm>
#include <utility>

#include "mysqlshdk/include/shellcore/scoped_contexts.h"
#include "mysqlshdk/include/shellcore/shell_resultset_dumper.h"
#ifdef HAVE_X_PROTOCOL
#include "mysqlshdk/libs/db/mysqlx/expr_parser.h"
#else
#include "mysqlshdk/libs/db/mysqlx/tokenizer.h"
#endif
#include "mysqlshdk/libs/utils/array_result.h"
#include "mysqlshdk/libs/utils/utils_general.h"
#include "mysqlshdk/libs/utils/utils_sqlstring.h"
#include "mysqlshdk/libs/utils/utils_string.h"

namespace mysqlsh {
namespace reports {

namespace {
using Resultset_writer_member = std::string (Resultset_writer::*)();

std::string resultset_formatter(const shcore::Array_t &report,
                                Resultset_writer_member member) {
  shcore::Array_as_result result{report};

  if (mysqlsh::current_shell_options()->get().gui_mode) {
    Gui_resultset_writer writer{&result};

    return (writer.*member)();
  } else {
    Resultset_writer writer{&result};

    return (writer.*member)();
  }
}

void merge_json_object(const shcore::Array_t &headers,
                       const shcore::Array_t &data,
                       const Column_definition &column,
                       shcore::Dictionary_t &&value) {
  // find the column name in the headers
  const auto pos = std::find_if(
      headers->begin(), headers->end(), [&column](const shcore::Value &v) {
        return (nullptr != column.name) && (v.get_string() == column.name);
      });
  // extra headers are only inserted if column name is found in headers, or
  // if headers are empty
  const auto insert_headers = (pos != headers->end()) || headers->empty();
  // if column name is not found, headers are going to be appended at the end,
  // otherwise they are going to be inserted in its place
  const auto insert_pos = pos == headers->end() ? pos : headers->erase(pos);

  shcore::Value::Array_type extra_headers;

  if (!column.mapping.empty()) {
    for (const auto &m : column.mapping) {
      extra_headers.emplace_back(m.name);
      data->emplace_back(std::move(value->find(m.id)->second));
    }
  } else {
    for (auto &v : *value) {
      extra_headers.emplace_back(std::move(v.first));
      data->emplace_back(std::move(v.second));
    }
  }

  if (insert_headers) {
    headers->insert(insert_pos, std::make_move_iterator(extra_headers.begin()),
                    std::make_move_iterator(extra_headers.end()));
  }
}

}  // namespace

std::string vertical_formatter(const shcore::Array_t &report) {
  return resultset_formatter(report, &Resultset_writer::write_vertical);
}

std::string table_formatter(const shcore::Array_t &report) {
  return resultset_formatter(report, &Resultset_writer::write_table);
}

std::string gui_table_formatter(const shcore::Array_t &report) {
  return resultset_formatter(report, &Gui_resultset_writer::writer_gui);
}

std::string brief_formatter(const shcore::Array_t &report) {
  shcore::Array_as_result result{report};
  std::string output;

  while (result.has_resultset()) {
    const auto &columns = result.get_metadata();
    const auto prev_size = output.size();

    while (const auto row = result.fetch_one()) {
      for (size_t i = 0; i < columns.size(); ++i) {
        output.append(columns[i].get_column_name())
            .append(": ")
            .append(row->get_as_string(i))
            .append(", ");
      }
    }

    if (output.size() > prev_size) {
      // remove space
      output.pop_back();
      // remove comma
      output.pop_back();
      output.append(".\n");
    }

    result.next_resultset();
  }

  return output;
}

std::string status_formatter(const shcore::Array_t &report) {
  return resultset_formatter(report, &Resultset_writer::write_status);
}

shcore::Array_t create_report_from_json_object(
    const std::shared_ptr<mysqlshdk::db::ISession> &session,
    const std::string &query, const std::vector<Column_definition> &columns,
    const std::vector<Column_definition> &return_columns) {
  std::string final_query;

  if (!columns.empty()) {
    std::string items;

    for (const auto &c : columns) {
      items.append("'").append(c.id).append("',").append(c.query).append(",");
    }

    if (!items.empty()) {
      // remove last comma
      items.pop_back();
    }

    final_query = "SELECT json_object(" + items + ") ";
  }

  final_query += query;

  const auto result = session->query(final_query);
  auto report = shcore::make_array();

  const auto &use_headers = return_columns.empty() ? columns : return_columns;

  // headers
  {
    auto headers = shcore::make_array();

    for (const auto &c : use_headers) {
      headers->emplace_back(c.name);
    }

    report->emplace_back(std::move(headers));
  }

  // write data
  while (const auto row = result->fetch_one()) {
    if (!row->is_null(0)) {
      auto output_row = shcore::make_array();
      auto map = shcore::Value::parse(row->get_string(0)).as_map();

      if (!use_headers.empty()) {
        output_row->reserve(use_headers.size());

        for (const auto &c : use_headers) {
          auto &value = map->find(c.id)->second;

          if (shcore::Value_type::Map == value.get_type()) {
            merge_json_object(report->at(0).as_array(), output_row, c,
                              value.as_map());
          } else {
            output_row->emplace_back(std::move(value));
          }
        }
      } else {
        merge_json_object(report->at(0).as_array(), output_row, {},
                          std::move(map));
      }

      report->emplace_back(std::move(output_row));
    }
  }

  return report;
}

Sql_condition::Sql_condition(const Sql_condition::Validator &ident,
                             const Sql_condition::Validator &op)
    : m_identifier(ident), m_operator(op) {}

std::string Sql_condition::identifier_validator(const std::string &ident) {
  return ident;
}

std::string Sql_condition::operator_validator(const std::string &ident) {
  static constexpr const char *allowed[] = {"==", "!=",   ">",  ">=", "<",
                                            "<=", "like", "&&", "||", "not"};

  if (std::end(allowed) ==
      std::find(std::begin(allowed), std::end(allowed), ident)) {
    throw std::invalid_argument("Disallowed operator: " + ident);
  }

  return "==" == ident ? "=" : ident;
}

#ifdef HAVE_X_PROTOCOL
std::string Sql_condition::parse(const std::string &condition) const {
  const auto expr = ::mysqlx::Expr_parser{condition}.expr();

  return validate_expression(*expr.get());
}

std::string Sql_condition::validate_expression(
    const Mysqlx::Expr::Expr &expr) const {
  switch (expr.type()) {
    case Mysqlx::Expr::Expr_Type::Expr_Type_IDENT: {
      const auto &ident = expr.identifier();
      const auto name =
          (ident.has_table_name() ? ident.table_name() + "." : "") +
          ident.name();
      return m_identifier(name);
    }

    case Mysqlx::Expr::Expr_Type::Expr_Type_LITERAL: {
      const auto &literal = expr.literal();

      switch (literal.type()) {
        case ::Mysqlx::Datatypes::Scalar_Type::Scalar_Type_V_NULL:
          return "NULL";

        case ::Mysqlx::Datatypes::Scalar_Type::Scalar_Type_V_BOOL:
          return literal.v_bool() ? "TRUE" : "FALSE";

        case ::Mysqlx::Datatypes::Scalar_Type::Scalar_Type_V_DOUBLE:
          return std::to_string(literal.v_double());

        case ::Mysqlx::Datatypes::Scalar_Type::Scalar_Type_V_SINT:
          return std::to_string(literal.v_signed_int());

        case ::Mysqlx::Datatypes::Scalar_Type::Scalar_Type_V_UINT:
          return std::to_string(literal.v_unsigned_int());

        case ::Mysqlx::Datatypes::Scalar_Type::Scalar_Type_V_OCTETS:
          return shcore::quote_sql_string(literal.v_octets().value());

        default:
          throw std::invalid_argument(
              "Unknown literal type: " +
              ::Mysqlx::Datatypes::Scalar_Type_Name(literal.type()));
      }
    }

    case Mysqlx::Expr::Expr_Type::Expr_Type_OPERATOR: {
      const auto &op = expr.operator_();
      const auto name = m_operator(op.name());

      if ("not" == name) {
        return name + "(" + validate_expression(op.param()[0]) + ")";
      } else {
        return "(" + validate_expression(op.param()[0]) + ")" + name + "(" +
               validate_expression(op.param()[1]) + ")";
      }
    }

    default:
      throw std::invalid_argument("Unknown expression type: " +
                                  ::Mysqlx::Expr::Expr_Type_Name(expr.type()));
  }
}
#else  // !HAVE_X_PROTOCOL
namespace {

using ::mysqlx::Token;
using Type = ::mysqlx::Token::Type;

/**
 * Parser of the conditions Sql_condition takes, for builds without the X
 * protocol expression parser. It reads the tokens of the X protocol tokenizer,
 * so both parsers agree on what is a name, a keyword, a number or a string.
 *
 * Grammar (keywords are case insensitive):
 *
 *   condition  ::= or_expr
 *   or_expr    ::= and_expr ((OR | "||") and_expr)*
 *   and_expr   ::= not_expr ((AND | "&&") not_expr)*
 *   not_expr   ::= (NOT | "!") not_expr | comparison
 *   comparison ::= operand [[NOT] LIKE operand | comparison_operator operand]
 *   operand    ::= "(" or_expr ")" | literal | name ["." name]
 *
 * Operators are handed to the operator validator under the names of the X
 * protocol parser ("==", "!=", "&&", "||", "not", "like"), so that one list
 * decides what is allowed; the SQL written uses AND, OR and NOT, as || is
 * string concatenation in some SQL modes.
 *
 * Unlike the X protocol parser, which applies a NOT prefix to the operand that
 * follows it, NOT applies to the whole comparison, as in SQL: "NOT a = 1" is
 * "NOT (a = 1)", not "(NOT a) = 1".
 */
class Condition_parser {
 public:
  Condition_parser(const std::string &input, const std::vector<Token> &tokens,
                   const Sql_condition::Validator &identifier,
                   const Sql_condition::Validator &op)
      : m_input(input),
        m_tokens(tokens),
        m_identifier(identifier),
        m_operator(op) {}

  std::string parse() {
    auto result = or_expr();

    if (!at_end()) {
      reject_operator();
      error("Expected end of expression");
    }

    return result;
  }

 private:
  bool at_end() const { return m_current >= m_tokens.size(); }

  bool is(Type type, size_t ahead = 0) const {
    return m_current + ahead < m_tokens.size() &&
           type == m_tokens[m_current + ahead].get_type();
  }

  const Token &next() { return m_tokens[m_current++]; }

  [[noreturn]] void error(const std::string &message) const {
    // the messages of the X protocol parser
    if (at_end()) throw ::mysqlx::Parser_error("Unexpected end of expression.");

    throw ::mysqlx::Parser_error(message, m_tokens[m_current], m_input);
  }

  /**
   * A name, or a keyword of the tokenizer which is not an operator or a
   * literal, as the X protocol parser takes those as names too ("time").
   */
  static bool is_name(const Token &token) {
    switch (token.get_type()) {
      case Type::IDENT:
        return true;

      case Type::NOT:
      case Type::AND:
      case Type::OR:
      case Type::XOR:
      case Type::IS:
      case Type::IN_:
      case Type::LIKE:
      case Type::REGEXP:
      case Type::BETWEEN:
      case Type::OVERLAPS:
      case Type::ESCAPE:
      case Type::INTERVAL:
      case Type::DIV:
      case Type::TRUE_:
      case Type::FALSE_:
      case Type::T_NULL:
        return false;

      default:
        return ::mysqlx::Tokenizer::map.reserved_words.count(token.get_text());
    }
  }

  /**
   * An operator where none is allowed: hands it to the operator validator,
   * which rejects it.
   */
  void reject_operator() const {
    if (at_end()) return;

    const auto &names = ::mysqlx::Tokenizer::map.operator_names;

    if (const auto it = names.find(m_tokens[m_current].get_text());
        names.end() != it) {
      m_operator(it->second);
    }
  }

  /**
   * Consumes a logical operator, written as a keyword or as a doubled symbol.
   */
  bool accept(Type keyword, Type symbol) {
    if (is(keyword)) {
      ++m_current;
      return true;
    }

    if (is(symbol) && is(symbol, 1)) {
      m_current += 2;
      return true;
    }

    return false;
  }

  std::string binary(std::string (Condition_parser::*next_level)(),
                     Type keyword, Type symbol, const char *name,
                     const char *sql) {
    auto result = (this->*next_level)();

    while (accept(keyword, symbol)) {
      m_operator(name);
      result = "(" + result + ") " + sql + " (" + (this->*next_level)() + ")";
    }

    return result;
  }

  std::string or_expr() {
    return binary(&Condition_parser::and_expr, Type::OR, Type::BITOR, "||",
                  "OR");
  }

  std::string and_expr() {
    return binary(&Condition_parser::not_expr, Type::AND, Type::BITAND, "&&",
                  "AND");
  }

  std::string not_expr() {
    if (is(Type::NOT) || is(Type::BANG)) {
      next();
      m_operator("not");
      return "NOT (" + not_expr() + ")";
    }

    return comparison();
  }

  std::string comparison() {
    auto left = operand();

    // NOT LIKE, which the X protocol parser takes as not(like)
    if (is(Type::NOT) && is(Type::LIKE, 1)) {
      next();
      next();
      m_operator("not");
      m_operator("like");
      return "NOT ((" + left + ") LIKE (" + operand() + "))";
    }

    // the names of the X protocol parser; the tokenizer reads "<>" as "!="
    static constexpr std::pair<Type, const char *> comparisons[] = {
        {Type::EQ, "=="}, {Type::NE, "!="}, {Type::LT, "<"}, {Type::LE, "<="},
        {Type::GT, ">"},  {Type::GE, ">="}, {Type::LIKE, "like"}};

    for (const auto &[type, name] : comparisons) {
      if (!is(type)) continue;

      next();
      // "==" comes as two tokens
      if (Type::EQ == type && is(Type::EQ)) next();

      auto op = m_operator(name);
      if ("like" == op) op = "LIKE";

      return "(" + left + ") " + op + " (" + operand() + ")";
    }

    return left;
  }

  std::string operand() {
    if (is(Type::LPAREN)) {
      next();
      auto result = or_expr();

      if (!is(Type::RPAREN)) {
        reject_operator();
        error("Expected ')'");
      }

      next();
      return result;
    }

    if (is(Type::MINUS) && (is(Type::LINTEGER, 1) || is(Type::LNUM, 1))) {
      next();
      return "-" + next().get_text();
    }

    if (is(Type::LINTEGER) || is(Type::LNUM)) return next().get_text();

    if (is(Type::LSTRING)) return shcore::quote_sql_string(next().get_text());

    if (is(Type::TRUE_) || is(Type::FALSE_) || is(Type::T_NULL)) {
      return shcore::str_upper(next().get_text());
    }

    if (!at_end() && is_name(m_tokens[m_current])) {
      auto name = next().get_text();

      if (is(Type::DOT)) {
        next();

        if (at_end() || !is_name(m_tokens[m_current])) {
          error("Expected an identifier");
        }

        name += "." + next().get_text();
      }

      return m_identifier(name);
    }

    reject_operator();
    error("Expected an operand");
  }

  const std::string &m_input;
  const std::vector<Token> &m_tokens;
  const Sql_condition::Validator &m_identifier;
  const Sql_condition::Validator &m_operator;
  size_t m_current = 0;
};

}  // namespace

std::string Sql_condition::parse(const std::string &condition) const {
  ::mysqlx::Tokenizer tokenizer{condition};
  tokenizer.get_tokens();
  const std::vector<Token> tokens{tokenizer.begin(), tokenizer.end()};

  // syntax first, names second, in the order of the X protocol parser: a
  // malformed condition is reported as such, not as an unknown column
  const Validator any_identifier = [](const std::string &ident) {
    return ident;
  };
  Condition_parser{condition, tokens, any_identifier, m_operator}.parse();

  return Condition_parser{condition, tokens, m_identifier, m_operator}.parse();
}
#endif  // HAVE_X_PROTOCOL

}  // namespace reports
}  // namespace mysqlsh
