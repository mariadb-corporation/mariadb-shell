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

#include "modules/mrs/core/mrs_strings.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>

namespace mrs {

std::string to_lower(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

std::string to_upper(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return result;
}

bool ends_with(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string join(const std::vector<std::string> &parts,
                 std::string_view separator) {
  std::string result;
  for (const auto &part : parts) {
    if (!result.empty()) result += separator;
    result += part;
  }
  return result;
}

std::vector<std::string> split(std::string_view text, char separator,
                               bool skip_empty) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= text.size()) {
    auto end = text.find(separator, start);
    if (end == std::string_view::npos) end = text.size();
    if (!skip_empty || end > start) {
      parts.emplace_back(text.substr(start, end - start));
    }
    start = end + 1;
  }
  return parts;
}

namespace {
constexpr std::string_view k_base64_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}  // namespace

std::string base64_encode(std::string_view data) {
  std::string result;
  result.reserve((data.size() + 2) / 3 * 4);

  size_t i = 0;
  for (; i + 3 <= data.size(); i += 3) {
    const uint32_t triple = (static_cast<unsigned char>(data[i]) << 16) |
                            (static_cast<unsigned char>(data[i + 1]) << 8) |
                            static_cast<unsigned char>(data[i + 2]);
    result += k_base64_alphabet[(triple >> 18) & 0x3f];
    result += k_base64_alphabet[(triple >> 12) & 0x3f];
    result += k_base64_alphabet[(triple >> 6) & 0x3f];
    result += k_base64_alphabet[triple & 0x3f];
  }

  const size_t rest = data.size() - i;
  if (rest == 1) {
    const uint32_t v = static_cast<unsigned char>(data[i]) << 16;
    result += k_base64_alphabet[(v >> 18) & 0x3f];
    result += k_base64_alphabet[(v >> 12) & 0x3f];
    result += "==";
  } else if (rest == 2) {
    const uint32_t v = (static_cast<unsigned char>(data[i]) << 16) |
                       (static_cast<unsigned char>(data[i + 1]) << 8);
    result += k_base64_alphabet[(v >> 18) & 0x3f];
    result += k_base64_alphabet[(v >> 12) & 0x3f];
    result += k_base64_alphabet[(v >> 6) & 0x3f];
    result += '=';
  }
  return result;
}

std::string base64_decode(std::string_view text) {
  std::string result;
  result.reserve(text.size() / 4 * 3);

  uint32_t accumulator = 0;
  int bits = 0;
  bool padding = false;
  for (const char c : text) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    if (c == '=') {
      padding = true;
      continue;
    }
    const auto pos = k_base64_alphabet.find(c);
    if (pos == std::string_view::npos || padding) {
      throw std::runtime_error("The content is not valid base64.");
    }
    accumulator = (accumulator << 6) | static_cast<uint32_t>(pos);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result += static_cast<char>((accumulator >> bits) & 0xff);
    }
  }
  return result;
}

}  // namespace mrs
