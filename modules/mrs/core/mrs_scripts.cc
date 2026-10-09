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

#include "modules/mrs/core/mrs_scripts.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <stdexcept>

#include "modules/mrs/core/mrs_metadata.h"
#include "modules/mrs/core/mrs_metadata_db_objects.h"
#include "modules/mrs/core/mrs_sql.h"

namespace mrs {
namespace scripts {

namespace {

constexpr std::string_view k_typescript = "TypeScript";

bool ends_with(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

bool is_identifier_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

std::string trim(std::string_view s) {
  size_t b = 0, e = s.size();
  while (b < e && is_space(s[b])) ++b;
  while (e > b && is_space(s[e - 1])) --e;
  return std::string(s.substr(b, e - b));
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

// The parser works on the blanked code and takes values from the original
// code at the same positions.
class Reader {
 public:
  Reader(std::string_view code, std::string_view blanked)
      : m_code(code), m_blanked(blanked) {}

  std::string_view code() const { return m_code; }
  std::string_view blanked() const { return m_blanked; }

  size_t skip_space(size_t pos) const {
    while (pos < m_blanked.size() && is_space(m_blanked[pos])) ++pos;
    return pos;
  }

  // The identifier at pos (empty if there is none).
  std::string identifier(size_t pos) const {
    size_t end = pos;
    while (end < m_blanked.size() && is_identifier_char(m_blanked[end])) ++end;
    return std::string(m_blanked.substr(pos, end - pos));
  }

  bool keyword_at(size_t pos, std::string_view word) const {
    return m_blanked.compare(pos, word.size(), word) == 0 &&
           (pos + word.size() >= m_blanked.size() ||
            !is_identifier_char(m_blanked[pos + word.size()]));
  }

  // The position after the bracket matching the opening one at pos, or npos.
  size_t matching(size_t pos) const {
    const char open = m_blanked[pos];
    const char close = open == '{' ? '}' : open == '(' ? ')' : open == '[' ? ']' : '>';
    int depth = 0;
    for (size_t i = pos; i < m_blanked.size(); ++i) {
      if (m_blanked[i] == open) {
        ++depth;
      } else if (m_blanked[i] == close && --depth == 0) {
        return i + 1;
      }
    }
    return std::string_view::npos;
  }

  int line_of(size_t pos) const {
    return static_cast<int>(std::count(m_code.begin(), m_code.begin() + pos, '\n')) + 1;
  }

  Position position(size_t start, size_t end) const {
    return {line_of(start), line_of(end), start, end};
  }

 private:
  std::string_view m_code;
  std::string_view m_blanked;
};

// Splits [start, end) at the separators that are not inside brackets.
std::vector<std::pair<size_t, size_t>> split_top_level(const Reader &r, size_t start,
                                                        size_t end,
                                                        std::string_view separators) {
  std::vector<std::pair<size_t, size_t>> parts;
  int depth = 0;
  size_t part_start = start;
  const auto text = r.blanked();
  for (size_t i = start; i < end; ++i) {
    const char c = text[i];
    if (c == '(' || c == '{' || c == '[' || c == '<') {
      ++depth;
    } else if ((c == ')' || c == '}' || c == ']' || (c == '>' && i > 0 && text[i - 1] != '=')) &&
               depth > 0) {
      --depth;
    } else if (depth == 0 && separators.find(c) != std::string_view::npos) {
      parts.emplace_back(part_start, i);
      part_start = i + 1;
    }
  }
  parts.emplace_back(part_start, end);
  return parts;
}

// The position of the first top-level occurrence of c in [start, end).
size_t find_top_level(const Reader &r, size_t start, size_t end, char c) {
  int depth = 0;
  const auto text = r.blanked();
  for (size_t i = start; i < end; ++i) {
    const char ch = text[i];
    if (depth == 0 && ch == c) return i;
    if (ch == '(' || ch == '{' || ch == '[' || ch == '<') {
      ++depth;
    } else if ((ch == ')' || ch == '}' || ch == ']' || (ch == '>' && text[i - 1] != '=')) &&
               depth > 0) {
      --depth;
    }
  }
  return std::string_view::npos;
}

std::optional<double> to_number(const std::string &text) {
  if (text.empty()) return std::nullopt;
  char *end = nullptr;
  const double v = std::strtod(text.c_str(), &end);
  if (end != text.c_str() + text.size()) return std::nullopt;
  return v;
}

json::Value number_value(double v) {
  if (v == static_cast<double>(static_cast<int64_t>(v))) {
    return json::Value(static_cast<int64_t>(v));
  }
  return json::Value(v);
}

// A JavaScript object or array literal as JSON: keys quoted, trailing
// commas removed, single quoted strings turned into double quoted ones.
std::string literal_as_json(std::string_view text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\'' || c == '"' || c == '`') {
      // a string: copy it with double quotes
      std::string value;
      size_t j = i + 1;
      for (; j < text.size() && text[j] != c; ++j) {
        if (text[j] == '\\' && j + 1 < text.size()) {
          value += text[j];
          value += text[++j];
        } else if (text[j] == '"') {
          value += "\\\"";
        } else {
          value += text[j];
        }
      }
      out += '"' + value + '"';
      i = j;
    } else if (c == ',') {
      size_t j = i + 1;
      while (j < text.size() && is_space(text[j])) ++j;
      if (j < text.size() && (text[j] == '}' || text[j] == ']')) continue;
      out += c;
    } else if ((std::isalpha(static_cast<unsigned char>(c)) || c == '_') &&
               (out.empty() || !is_identifier_char(out.back()))) {
      // a bare key: quote it when a colon follows
      size_t j = i;
      while (j < text.size() && is_identifier_char(text[j])) ++j;
      size_t k = j;
      while (k < text.size() && is_space(text[k])) ++k;
      const auto word = text.substr(i, j - i);
      if (k < text.size() && text[k] == ':') {
        out += '"' + std::string(word) + '"';
      } else {
        out += word;
      }
      i = j - 1;
    } else {
      out += c;
    }
  }
  return out;
}

// The value of a decorator property as the Python plugin read it: strings
// without quotes, object and array literals as JSON, true/false, numbers,
// anything else (e.g. MrsScriptFunctionType.BeforeUpdate) as text.
json::Value decorator_value(const std::string &text) {
  if (text.empty()) return json::Value("");
  const char first = text[0];
  if (first == '"' || first == '\'' || first == '`') {
    return json::Value(text.substr(1, text.size() >= 2 ? text.size() - 2 : 0));
  }
  if (first == '{' || first == '[') {
    if (auto doc = json::try_parse(literal_as_json(text))) return std::move(*doc);
    return json::Value(text);
  }
  if (lower(text) == "true") return json::Value(true);
  if (lower(text) == "false") return json::Value(false);
  if (const auto v = to_number(text)) return number_value(*v);
  return json::Value(text);
}

// The properties of the decorator object literal in [start, end), the
// braces excluded.
Properties decorator_properties(const Reader &r, size_t start, size_t end) {
  Properties props;
  for (const auto &[a, b] : split_top_level(r, start, end, ",")) {
    const size_t colon = find_top_level(r, a, b, ':');
    if (colon == std::string_view::npos) continue;
    auto name = trim(r.code().substr(a, colon - a));
    if (name.size() >= 2 && (name[0] == '"' || name[0] == '\'')) {
      name = name.substr(1, name.size() - 2);
    }
    if (name.empty()) continue;
    props.emplace_back(name, decorator_value(trim(r.code().substr(colon + 1, b - colon - 1))));
  }
  return props;
}

std::vector<Parameter> function_parameters(const Reader &r, size_t start, size_t end) {
  std::vector<Parameter> params;
  for (const auto &[a, b] : split_top_level(r, start, end, ",")) {
    if (trim(r.blanked().substr(a, b - a)).empty()) continue;
    Parameter p;
    size_t name_end = b;
    const size_t eq = find_top_level(r, a, b, '=');
    if (eq != std::string_view::npos) {
      name_end = eq;
      const auto text = trim(r.code().substr(eq + 1, b - eq - 1));
      p.optional = true;
      std::string default_type = "unknown";
      if (const auto v = to_number(text)) {
        p.default_value = number_value(*v);
        default_type = "number";
      } else if (text == "true" || text == "false") {
        p.default_value = json::Value(text == "true");
        default_type = "boolean";
      } else if (!text.empty() && (text[0] == '\'' || text[0] == '"' || text[0] == '`')) {
        p.default_value = json::Value(text.substr(1, text.size() - 2));
        default_type = "string";
      } else {
        p.default_value = json::Value(text);
      }
      p.type = default_type;
    }
    const size_t colon = find_top_level(r, a, name_end, ':');
    if (colon != std::string_view::npos) {
      p.type = trim(r.code().substr(colon + 1, name_end - colon - 1));
      name_end = colon;
    }
    p.name = trim(r.code().substr(a, name_end - a));
    if (!p.name.empty() && p.name.back() == '?') {
      p.optional = true;
      p.name = trim(p.name.substr(0, p.name.size() - 1));
    }
    if (p.type.empty()) p.type = "unknown";
    if (ends_with(p.type, "[]")) {
      p.type = p.type.substr(0, p.type.size() - 2);
      p.is_array = true;
    }
    params.push_back(std::move(p));
  }
  return params;
}

// Promise<T> -> T
std::string without_promise(std::string type) {
  type = trim(type);
  if (type.rfind("Promise", 0) == 0) {
    const auto open = type.find('<');
    const auto close = type.rfind('>');
    if (open != std::string::npos && close != std::string::npos && close > open) {
      return trim(type.substr(open + 1, close - open - 1));
    }
  }
  return type;
}

// @Mrs.<kind>({ ... }) at pos: the kind, the property range and the
// position after the closing parenthesis.
struct Decorator {
  std::string kind;
  size_t props_start = 0;
  size_t props_end = 0;
  size_t end = 0;
};

std::optional<Decorator> decorator_at(const Reader &r, size_t pos) {
  const auto text = r.blanked();
  if (text.compare(pos, 5, "@Mrs.") != 0) return std::nullopt;
  Decorator d;
  d.kind = r.identifier(pos + 5);
  size_t i = r.skip_space(pos + 5 + d.kind.size());
  if (i >= text.size() || text[i] != '(') return std::nullopt;
  const size_t paren_end = r.matching(i);
  if (paren_end == std::string_view::npos) return std::nullopt;
  i = r.skip_space(i + 1);
  if (i < text.size() && text[i] == '{') {
    const size_t brace_end = r.matching(i);
    if (brace_end == std::string_view::npos || brace_end > paren_end) return std::nullopt;
    d.props_start = i + 1;
    d.props_end = brace_end - 1;
  } else {
    d.props_start = d.props_end = i;
  }
  d.end = paren_end;
  return d;
}

std::optional<Script> script_at(const Reader &r, const Decorator &d, size_t start) {
  const auto text = r.blanked();
  size_t i = r.skip_space(d.end);
  for (const char *word : {"public", "static", "async"}) {
    if (r.keyword_at(i, word)) i = r.skip_space(i + std::string_view(word).size());
  }
  Script s;
  s.function_name = r.identifier(i);
  if (s.function_name.empty()) return std::nullopt;
  i = r.skip_space(i + s.function_name.size());
  if (i >= text.size() || text[i] != '(') return std::nullopt;
  const size_t params_end = r.matching(i);
  if (params_end == std::string_view::npos) return std::nullopt;
  s.parameters = function_parameters(r, i + 1, params_end - 1);

  // ": <return type> {" -- the type may hold < > and { } of its own
  i = r.skip_space(params_end);
  std::string return_type = "void";
  if (i < text.size() && text[i] == ':') {
    size_t body = find_top_level(r, i + 1, text.size(), '{');
    if (body == std::string_view::npos) return std::nullopt;
    return_type = r.code().substr(i + 1, body - i - 1);
    i = body;
  }
  i = r.skip_space(i);
  if (i >= text.size() || text[i] != '{') return std::nullopt;
  const size_t body_end = r.matching(i);
  if (body_end == std::string_view::npos) return std::nullopt;

  s.return_type = without_promise(return_type);
  if (ends_with(s.return_type, "[]")) {
    s.return_type = s.return_type.substr(0, s.return_type.size() - 2);
    s.returns_array = true;
  }
  s.properties = decorator_properties(r, d.props_start, d.props_end);
  s.position = r.position(start, body_end);
  return s;
}

void find_modules(const Code_file &file, const Reader &r, std::vector<Module> *out) {
  const auto text = r.blanked();
  for (size_t pos = text.find("@Mrs."); pos != std::string_view::npos;
       pos = text.find("@Mrs.", pos + 1)) {
    const auto d = decorator_at(r, pos);
    if (!d || (d->kind != "module" && d->kind != "schema")) continue;
    size_t i = r.skip_space(d->end);
    for (const char *word : {"export", "default"}) {
      if (r.keyword_at(i, word)) i = r.skip_space(i + std::string_view(word).size());
    }
    if (!r.keyword_at(i, "class")) continue;
    i = r.skip_space(i + 5);
    Module m;
    m.file = &file;
    m.class_name = r.identifier(i);
    if (m.class_name.empty()) continue;
    const size_t body = text.find('{', i + m.class_name.size());
    if (body == std::string_view::npos) continue;
    const size_t body_end = r.matching(body);
    if (body_end == std::string_view::npos) continue;

    m.schema_type = d->kind == "module" ? "SCRIPT_MODULE" : "DATABASE_SCHEMA";
    m.properties = decorator_properties(r, d->props_start, d->props_end);
    m.position = r.position(pos, body_end);

    for (size_t s = text.find("@Mrs.", body); s != std::string_view::npos && s < body_end;
         s = text.find("@Mrs.", s + 1)) {
      const auto sd = decorator_at(r, s);
      if (!sd || (sd->kind != "script" && sd->kind != "trigger")) continue;
      if (auto script = script_at(r, *sd, s)) {
        (sd->kind == "trigger" ? m.triggers : m.scripts).push_back(std::move(*script));
      }
    }
    out->push_back(std::move(m));
    pos = body_end - 1;
  }
}

void find_interfaces(const Code_file &file, const Reader &r, std::vector<Interface> *out) {
  const auto text = r.blanked();
  for (size_t pos = text.find("export"); pos != std::string_view::npos;
       pos = text.find("export", pos + 1)) {
    if (!r.keyword_at(pos, "export") || (pos > 0 && is_identifier_char(text[pos - 1]))) {
      continue;
    }
    size_t i = r.skip_space(pos + 6);
    if (!r.keyword_at(i, "interface")) continue;
    i = r.skip_space(i + 9);
    Interface f;
    f.file = &file;
    f.name = r.identifier(i);
    if (f.name.empty()) continue;
    i = r.skip_space(i + f.name.size());
    if (r.keyword_at(i, "extends")) {
      i = r.skip_space(i + 7);
      const size_t brace = text.find('{', i);
      if (brace == std::string_view::npos) continue;
      f.extends = trim(r.code().substr(i, brace - i));
      i = brace;
    }
    if (i >= text.size() || text[i] != '{') continue;
    const size_t body_end = r.matching(i);
    if (body_end == std::string_view::npos) continue;

    for (const auto &[a, b] : split_top_level(r, i + 1, body_end - 1, ";,\n")) {
      if (trim(r.blanked().substr(a, b - a)).empty()) continue;
      Interface_property p;
      size_t start = r.skip_space(a);
      if (r.keyword_at(start, "readonly")) {
        p.read_only = true;
        start = r.skip_space(start + 8);
      }
      size_t name_end;
      if (start < b && text[start] == '[') {
        // [key: type]: value
        const size_t close = r.matching(start);
        if (close == std::string_view::npos || close > b) continue;
        const size_t colon = find_top_level(r, start + 1, close - 1, ':');
        if (colon == std::string_view::npos) continue;
        p.name = trim(r.code().substr(start + 1, colon - start - 1));
        p.index_signature_type = trim(r.code().substr(colon + 1, close - colon - 2));
        name_end = close;
      } else {
        const size_t colon = find_top_level(r, start, b, ':');
        if (colon == std::string_view::npos) continue;
        p.name = trim(r.code().substr(start, colon - start));
        name_end = colon;
      }
      size_t colon = find_top_level(r, name_end, b, ':');
      if (colon == std::string_view::npos) continue;
      if (!p.index_signature_type && !p.name.empty() && p.name.back() == '?') {
        p.optional = true;
        p.name = trim(p.name.substr(0, p.name.size() - 1));
      } else if (p.index_signature_type && colon > 0 && text[colon - 1] == '?') {
        p.optional = true;
      }
      p.type = trim(r.code().substr(colon + 1, b - colon - 1));
      f.properties.push_back(std::move(p));
    }
    f.position = r.position(pos, body_end);
    out->push_back(std::move(f));
    pos = body_end - 1;
  }
}

bool is_simple_type(const std::string &type) {
  return type == "boolean" || type == "number" || type == "string";
}

const Interface *find_interface(const std::vector<Interface> &list, const std::string &name) {
  for (const auto &f : list) {
    if (f.name == name) return &f;
  }
  return nullptr;
}

// Adds the interface of a type (and the ones it extends) to the used list;
// false when the type is unknown.
bool use_type(const std::string &type, const std::vector<Interface> &all,
              std::vector<Interface> *used) {
  if (is_simple_type(type)) return true;
  if (find_interface(*used, type)) return true;
  const auto *f = find_interface(all, type);
  if (!f) return false;
  used->push_back(*f);
  if (f->extends) use_type(*f->extends, all, used);
  return true;
}

json::Value file_info_json(const Code_file &file) {
  json::Value doc = json::Value::object();
  const auto slash = file.path.rfind('/');
  doc.set("full_file_name", file.path);
  doc.set("relative_file_name", file.path);
  doc.set("file_name", slash == std::string::npos ? file.path : file.path.substr(slash + 1));
  doc.set("last_modification", file.last_modification);
  return doc;
}

json::Value position_json(const Position &p) {
  json::Value doc = json::Value::object();
  doc.set("line_number_start", p.line_start);
  doc.set("line_number_end", p.line_end);
  doc.set("character_start", static_cast<int64_t>(p.character_start));
  doc.set("character_end", static_cast<int64_t>(p.character_end));
  return doc;
}

json::Value properties_json(const Properties &props) {
  json::Value::Array list;
  for (const auto &[name, value] : props) {
    json::Value p = json::Value::object();
    p.set("name", name);
    p.set("value", value);
    list.push_back(std::move(p));
  }
  return json::Value(std::move(list));
}

json::Value script_json(const Script &s) {
  json::Value doc = json::Value::object();
  doc.set("function_name", s.function_name);
  doc.set("code_position", position_json(s.position));
  json::Value::Array params;
  for (const auto &p : s.parameters) {
    json::Value param = json::Value::object();
    param.set("name", p.name);
    param.set("type", p.type);
    param.set("optional", p.optional);
    param.set("is_array", p.is_array);
    if (p.default_value) param.set("default", *p.default_value);
    params.push_back(std::move(param));
  }
  doc.set("parameters", json::Value(std::move(params)));
  json::Value result = json::Value::object();
  result.set("type", s.return_type);
  result.set("is_array", s.returns_array);
  doc.set("return_type", std::move(result));
  doc.set("properties", properties_json(s.properties));
  return doc;
}

}  // namespace

bool is_script_file(std::string_view path) {
  return (ends_with(path, ".ts") || ends_with(path, ".mts")) &&
         !ends_with(path, ".spec.ts") && !ends_with(path, ".spec.mts") &&
         !ends_with(path, ".d.ts");
}

std::string blank_comments_and_strings(std::string_view code) {
  std::string out(code);
  const auto blank = [&out](size_t from, size_t to) {
    for (size_t i = from; i < to; ++i) {
      if (out[i] != '\n' && out[i] != '\r') out[i] = ' ';
    }
  };
  size_t i = 0;
  while (i < code.size()) {
    const char c = code[i];
    if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') {
      size_t end = code.find('\n', i);
      if (end == std::string_view::npos) end = code.size();
      blank(i + 2, end);
      i = end;
    } else if (c == '/' && i + 1 < code.size() && code[i + 1] == '*') {
      size_t end = code.find("*/", i + 2);
      end = end == std::string_view::npos ? code.size() : end;
      blank(i + 2, end);
      i = end + 2;
    } else if (c == '"' || c == '\'' || c == '`') {
      size_t j = i + 1;
      while (j < code.size() && code[j] != c) {
        if (code[j] == '\\') ++j;
        ++j;
      }
      blank(i + 1, std::min(j, code.size()));
      i = j + 1;
    } else {
      ++i;
    }
  }
  return out;
}

bool defines_mrs_module(std::string_view code) {
  const auto blanked = blank_comments_and_strings(code);
  const Reader r(code, blanked);
  std::vector<Module> modules;
  const Code_file file{"", std::string(code), ""};
  find_modules(file, r, &modules);
  return !modules.empty();
}

Definitions analyze_typescript(const std::vector<Code_file> &files) {
  Definitions defs;
  std::vector<Interface> all_interfaces;
  for (const auto &file : files) {
    const auto blanked = blank_comments_and_strings(file.code);
    const Reader r(file.code, blanked);
    find_interfaces(file, r, &all_interfaces);
    find_modules(file, r, &defs.modules);
  }

  // Only the interfaces the scripts use, and each one known
  for (const auto &m : defs.modules) {
    for (const auto &s : m.scripts) {
      if (s.return_type != "void" &&
          !use_type(s.return_type, all_interfaces, &defs.interfaces)) {
        defs.errors.push_back("The script " + s.function_name +
                              " returns an unknown datatype `" + s.return_type + "`.");
      }
      for (const auto &p : s.parameters) {
        if (!use_type(p.type, all_interfaces, &defs.interfaces)) {
          defs.errors.push_back("Unknown datatype `" + p.type +
                                "` used for script parameter `" + p.name + "`.");
        }
      }
    }
  }
  for (size_t i = 0; i < defs.interfaces.size(); ++i) {
    const auto properties = defs.interfaces[i].properties;
    for (const auto &p : properties) {
      auto type = p.type;
      if (ends_with(type, "[]")) type = type.substr(0, type.size() - 2);
      if (!use_type(type, all_interfaces, &defs.interfaces)) {
        defs.errors.push_back("Unknown datatype `" + type +
                              "` used for interface property `" + p.name + "`.");
      }
    }
  }
  return defs;
}

json::Value Definitions::to_json() const {
  json::Value::Array module_list;
  for (const auto &m : modules) {
    json::Value doc = json::Value::object();
    doc.set("file_info", file_info_json(*m.file));
    doc.set("class_name", m.class_name);
    doc.set("schema_type", m.schema_type);
    doc.set("code_position", position_json(m.position));
    doc.set("properties", properties_json(m.properties));
    json::Value::Array scripts, triggers;
    for (const auto &s : m.scripts) scripts.push_back(script_json(s));
    for (const auto &s : m.triggers) triggers.push_back(script_json(s));
    doc.set("scripts", json::Value(std::move(scripts)));
    doc.set("triggers", json::Value(std::move(triggers)));
    module_list.push_back(std::move(doc));
  }
  json::Value::Array interface_list;
  for (const auto &f : interfaces) {
    json::Value doc = json::Value::object();
    doc.set("file_info", file_info_json(*f.file));
    doc.set("name", f.name);
    doc.set("code_position", position_json(f.position));
    json::Value::Array props;
    for (const auto &p : f.properties) {
      json::Value prop = json::Value::object();
      prop.set("name", p.name);
      prop.set("type", p.type);
      prop.set("optional", p.optional);
      prop.set("readOnly", p.read_only);
      if (p.index_signature_type) prop.set("indexSignatureType", *p.index_signature_type);
      props.push_back(std::move(prop));
    }
    doc.set("properties", json::Value(std::move(props)));
    if (f.extends) doc.set("extends", *f.extends);
    interface_list.push_back(std::move(doc));
  }
  json::Value::Array error_list;
  for (const auto &e : errors) {
    json::Value doc = json::Value::object();
    doc.set("kind", "TypeError");
    doc.set("message", e);
    error_list.push_back(std::move(doc));
  }
  json::Value doc = json::Value::object();
  doc.set("script_modules", json::Value(std::move(module_list)));
  doc.set("interfaces", json::Value(std::move(interface_list)));
  doc.set("errors", json::Value(std::move(error_list)));
  doc.set("language", std::string(k_typescript));
  return doc;
}

}  // namespace scripts

namespace metadata {

namespace {

using scripts::Code_file;
using scripts::Properties;

std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool ends_with_text(const std::string &s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_simple_type_name(const std::string &type) {
  return type == "boolean" || type == "number" || type == "string";
}

json::Value file_info(const Code_file &file) {
  json::Value doc = json::Value::object();
  doc.set("full_file_name", file.path);
  doc.set("relative_file_name", file.path);
  doc.set("file_name", file.path.substr(file.path.rfind('/') + 1));
  doc.set("last_modification", file.last_modification);
  return doc;
}

bool is_build_folder(const std::string &dir) {
  const auto d = lower_copy(dir);
  return d == "build" || d == "output" || d == "out" || d == "dist";
}

bool is_static_folder(const std::string &dir) {
  const auto d = lower_copy(dir);
  return d == "static" || d == "assets" || d == "media" || d == "web" ||
         d == "js" || d == "css" || d == "images";
}

const json::Value *property(const Properties &props, std::string_view name) {
  for (const auto &[n, v] : props) {
    if (n == name) return &v;
  }
  return nullptr;
}

std::string text_property(const Properties &props, std::string_view name,
                          const std::string &default_value) {
  const auto *v = property(props, name);
  return v && v->is_string() ? v->as_string() : default_value;
}

std::optional<std::string> optional_text_property(const Properties &props,
                                                  std::string_view name) {
  const auto *v = property(props, name);
  if (!v || v->is_null()) return std::nullopt;
  return v->is_string() ? v->as_string() : v->dump();
}

bool bool_property(const Properties &props, std::string_view name, bool default_value) {
  const auto *v = property(props, name);
  return v && v->is_bool() ? v->as_bool() : default_value;
}

// The options of a module or script with its grants added.
std::optional<std::string> options_with_grants(const Properties &props) {
  const auto *options = property(props, "options");
  const auto *grants = property(props, "grants");
  if (!options && !grants) return std::nullopt;
  json::Value doc = options && options->is_object() ? *options : json::Value::object();
  if (grants) doc.set("grants", *grants);
  return doc.dump();
}

// /my_module/sub -> myModuleSub (the first part keeps its case)
std::string path_to_camel_case(std::string path) {
  if (!path.empty() && path[0] == '/') path.erase(0, 1);
  std::string out;
  bool upper = false;
  for (const char c : path) {
    if (c == '/' || c == '_') {
      upper = !out.empty();
    } else {
      out += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
      upper = false;
    }
  }
  return out;
}

std::string database_type(const std::string &type) {
  const auto t = lower_copy(type);
  if (t == "string") return "text";
  if (t == "number") return "decimal";
  if (t == "boolean") return "bit(1)";
  return "json";
}

json::Value column(const std::string &name, const std::string &type, bool not_null,
                   bool is_array) {
  json::Value doc = json::Value::object();
  doc.set("name", name);
  doc.set("not_null", not_null);
  doc.set("datatype", database_type(type));
  doc.set("is_array", is_array);
  if (!is_simple_type_name(type)) doc.set("interface", type);
  return doc;
}

Object_field field(Db_session *session, const Id &object_id, const std::string &name,
                   int position, json::Value db_column) {
  Object_field f;
  f.id = new_id(session);
  f.object_id = object_id;
  f.name = name;
  f.position = position;
  f.db_column = std::move(db_column);
  f.enabled = true;
  f.allow_filtering = true;
  return f;
}

json::Value class_sdk_options(const std::string &class_name) {
  json::Value language = json::Value::object();
  language.set("language", "TypeScript");
  language.set("class_name", class_name);
  json::Value doc = json::Value::object();
  doc.set("language_options", json::Value(json::Value::Array{std::move(language)}));
  doc.set("class_name", class_name);
  return doc;
}

// The fields of an interface (with the ones it extends); properties of an
// interface type become references, as the Python plugin stored them.
void add_interface_fields(Db_session *session, const scripts::Definitions &defs,
                          const std::string &name, const Id &object_id,
                          std::vector<Object_field> *fields, int depth = 0) {
  const scripts::Interface *f = nullptr;
  for (const auto &i : defs.interfaces) {
    if (i.name == name) f = &i;
  }
  if (!f || depth > 16) return;
  // The interface's own properties first, then the inherited ones
  for (const auto &p : f->properties) {
    auto type = p.type;
    bool is_array = false;
    if (type.size() > 2 && type.compare(type.size() - 2, 2, "[]") == 0) {
      type = type.substr(0, type.size() - 2);
      is_array = true;
    }
    auto db_column = column(p.name, type, !p.optional, is_array);
    db_column.set("read_only", p.read_only);
    auto object_field = field(session, object_id, p.name,
                              static_cast<int>(fields->size()), std::move(db_column));
    if (!is_simple_type_name(type)) {
      Object_reference ref;
      ref.id = new_id(session);
      json::Value mapping = json::Value::object();
      mapping.set("kind", is_array ? "1:n" : "1:1");
      mapping.set("constraint", "interface");
      mapping.set("referenced_schema", type);
      mapping.set("referenced_table", "");
      json::Value pair = json::Value::object();
      pair.set("base", "n/a");
      pair.set("ref", "n/a");
      mapping.set("column_mapping", json::Value(json::Value::Array{std::move(pair)}));
      ref.reference_mapping = std::move(mapping);
      ref.sdk_options = class_sdk_options(type).dump();
      object_field.reference = std::move(ref);
    }
    fields->push_back(std::move(object_field));
  }
  if (f->extends) {
    add_interface_fields(session, defs, *f->extends, object_id, fields, depth + 1);
  }
}

}  // namespace

Registered_scripts register_scripts(Db_session *session,
                                    const Content_set &content_set,
                                    const std::optional<std::string> &language) {
  if (language && *language != "TypeScript") {
    throw std::runtime_error("The MRS scripting language " + *language +
                             " is not supported.");
  }
  const auto service = get_service(session, content_set.service_id);
  if (!service) throw std::runtime_error("The content set's service was not found.");

  // The script files, the build output and the folders served as is
  std::vector<Code_file> code_files;
  std::optional<std::string> build_folder;
  std::set<std::string> static_folders;
  for (const auto &file : get_content_files(session, content_set.id, true)) {
    std::vector<std::string> parts;
    for (size_t start = 1, end; start <= file.request_path.size(); start = end + 1) {
      end = file.request_path.find('/', start);
      if (end == std::string::npos) end = file.request_path.size();
      parts.push_back(file.request_path.substr(start, end - start));
    }
    for (size_t k = 0; k + 1 < parts.size() && k < 2; ++k) {
      if (is_build_folder(parts[k])) build_folder = parts[k];
      if (is_static_folder(parts[k])) static_folders.insert(parts[k]);
    }
    if (!scripts::is_script_file(file.request_path)) continue;
    const auto &content = file.content.value_or("");
    if (!is_text(content) && !content.empty()) {
      throw std::runtime_error("The content of file " + file.request_path +
                               " is binary data, not text.");
    }
    std::string last_modification;
    if (const auto options = file.options ? json::try_parse(*file.options) : std::nullopt) {
      last_modification = options->get_string("last_modification");
    }
    code_files.push_back({file.request_path, content, last_modification});
  }

  std::vector<Code_file> module_files;
  for (const auto &file : code_files) {
    if (scripts::defines_mrs_module(file.code)) module_files.push_back(file);
  }
  if (module_files.empty()) {
    throw std::runtime_error("The content set holds no MRS scripts: no TypeScript "
                             "file defines an @Mrs.module class.");
  }
  if (!build_folder) {
    throw std::runtime_error(
        "No build folder (build, dist, out or output) was found for this "
        "TypeScript project. Please upload the build output as well.");
  }

  const auto defs = scripts::analyze_typescript(code_files);
  if (!defs.errors.empty()) {
    std::string message = "The MRS scripts have errors:";
    for (const auto &e : defs.errors) message += "\n" + e;
    throw std::runtime_error(message);
  }

  // Replace what an earlier LOAD SCRIPTS registered
  delete_registered_scripts(session, content_set);

  Registered_scripts registered;
  json::Value::Array module_files_doc;
  for (const auto &m : defs.modules) {
    const auto &props = m.properties;

    // The compiled module the daemon loads
    std::string file_to_load;
    if (const auto output = optional_text_property(props, "outputFilePath")) {
      file_to_load = *output;
      std::replace(file_to_load.begin(), file_to_load.end(), '\\', '/');
      if (file_to_load.empty() || file_to_load[0] != '/') file_to_load = "/" + file_to_load;
    } else {
      const auto slash = m.file->path.rfind('/');
      auto name = m.file->path.substr(slash + 1);
      if (ends_with_text(name, ".mts")) {
        name = name.substr(0, name.size() - 4) + ".mjs";
      } else if (ends_with_text(name, ".ts")) {
        name = name.substr(0, name.size() - 3) + ".js";
      }
      file_to_load = "/" + *build_folder + "/" + name;
    }
    json::Value module_file = json::Value::object();
    module_file.set("file_info", file_info(*m.file));
    module_file.set("file_to_load", file_to_load);
    module_file.set("class_name", m.class_name);
    module_files_doc.push_back(std::move(module_file));

    const auto request_path = text_property(props, "requestPath", "/" + m.class_name);
    auto schema = find_schema(session, content_set.service_id, request_path);
    if (schema && schema->schema_type != "SCRIPT_MODULE") {
      throw std::runtime_error("The request path " + request_path +
                               " of the MRS module " + m.class_name +
                               " is used by a REST schema.");
    }
    if (!schema) {
      Schema_definition definition;
      definition.service_id = content_set.service_id;
      definition.name = text_property(props, "name", path_to_camel_case(request_path));
      definition.request_path = request_path;
      definition.enabled = bool_property(props, "enabled", true) ? 1 : 0;
      definition.internal = bool_property(props, "internal", true);
      definition.requires_auth = bool_property(props, "requiresAuth", false);
      definition.options = options_with_grants(props);
      definition.metadata = optional_text_property(props, "metadata");
      definition.comments = optional_text_property(props, "comments");
      definition.schema_type = "SCRIPT_MODULE";
      schema = get_schema(session, add_schema(session, definition));
    }
    ++registered.modules;

    for (const auto &s : m.scripts) {
      const auto &fprops = s.properties;
      const auto path = text_property(fprops, "requestPath", "/" + s.function_name);
      const auto full_path = service->url_context_root + schema->request_path + path;
      const auto row_ownership = optional_text_property(fprops, "rowOwnershipParameter");

      std::vector<Object_definition> objects(2);
      auto &params = objects[0];
      params.id = new_id(session);
      params.name = path_to_pascal_case(full_path) + "Params";
      params.kind = "PARAMETERS";
      params.position = 0;
      for (const auto &p : s.parameters) {
        auto db_column = column(p.name, p.type, !p.optional, p.is_array);
        db_column.set("in", true);
        if (p.default_value) db_column.set("default", *p.default_value);
        auto f = field(session, params.id, p.name,
                       static_cast<int>(params.fields.size()), std::move(db_column));
        if (row_ownership && p.name == *row_ownership) params.row_ownership_field_id = f.id;
        params.fields.push_back(std::move(f));
      }

      auto &result = objects[1];
      result.id = new_id(session);
      result.name = path_to_pascal_case(full_path) + "Result";
      result.kind = "RESULT";
      result.position = 1;
      if (is_simple_type_name(s.return_type)) {
        result.fields.push_back(field(session, result.id, "result", 0,
                                      column("result", s.return_type, true,
                                             s.returns_array)));
      } else if (s.return_type != "void") {
        add_interface_fields(session, defs, s.return_type, result.id, &result.fields);
      }
      auto sdk = class_sdk_options(s.return_type);
      sdk.set("returns_array", s.returns_array);
      result.sdk_options = sdk.dump();

      Db_object_definition definition;
      definition.db_schema_id = schema->id;
      definition.name = text_property(fprops, "name", s.function_name);
      definition.request_path = path;
      definition.object_type = "SCRIPT";
      definition.enabled = bool_property(fprops, "enabled", true) ? 1 : 0;
      definition.internal = bool_property(fprops, "internal", true);
      definition.requires_auth = bool_property(fprops, "requiresAuth", true);
      definition.options = options_with_grants(fprops);
      definition.metadata = optional_text_property(fprops, "metadata");
      definition.comments = optional_text_property(fprops, "comments");
      definition.format = text_property(fprops, "format", "FEED");
      definition.media_type = optional_text_property(fprops, "mediaType");
      const Id db_object_id = add_db_object(session, definition, objects);

      json::Value link_options = json::Value::object();
      link_options.set("file_to_load", file_to_load);
      session->execute(sql::Insert("content_set_has_obj_def")
                           .set("content_set_id", sql::Value::id(content_set.id))
                           .set("db_object_id", sql::Value::id(db_object_id))
                           .set("kind", "Script")
                           .set("priority", 0)
                           .set("language", "TypeScript")
                           .set("name", s.function_name)
                           .set("class_name", m.class_name)
                           .set("options", link_options.dump())
                           .str());

      for (const auto &grant : option_grant_statements(definition.options)) {
        session->execute(grant);
      }
      ++registered.scripts;
    }
  }

  // The set holds scripts now: its options carry what the daemon loads
  json::Value options = json::Value::object();
  if (content_set.options) {
    if (auto doc = json::try_parse(*content_set.options); doc && doc->is_object()) {
      options = std::move(*doc);
    }
  }
  options.set("contains_mrs_scripts", true);
  options.set("mrs_scripting_language", "TypeScript");
  options.set("script_module_files", json::Value(std::move(module_files_doc)));
  auto definitions = defs.to_json();
  definitions.set("build_folder", *build_folder);
  if (!static_folders.empty()) {
    json::Value::Array folders;
    for (const auto &f : static_folders) folders.emplace_back(f);
    definitions.set("static_content_folders", json::Value(std::move(folders)));
  }
  options.set("script_definitions", std::move(definitions));
  Content_set_changes changes;
  changes.content_type = "SCRIPTS";
  changes.options = options.dump();
  update_content_set(session, content_set.id, changes);

  // Only the static folders are served; the sources and the build output
  // are private (enabled = 2), the daemon still reads them
  std::string served;
  for (const auto &f : static_folders) {
    served += (served.empty() ? "" : " OR ") + std::string("request_path LIKE ") +
              sql::quote("/" + f + "/%");
  }
  session->execute("UPDATE " + sql::metadata_table("content_file") +
                   " SET enabled = 2 WHERE content_set_id = " + sql::id(content_set.id) +
                   (served.empty() ? "" : " AND NOT (" + served + ")"));
  return registered;
}

}  // namespace metadata
}  // namespace mrs
