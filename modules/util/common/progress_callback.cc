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

#include "modules/util/common/progress_callback.h"

#include <utility>

#include "mysqlshdk/libs/utils/logger.h"
#include "mysqlshdk/libs/utils/utils_string.h"

#include "modules/util/common/common_options.h"
#include "modules/util/dump/console_with_progress.h"

namespace mysqlsh {
namespace common {

Progress_callback_console::Progress_callback_console(
    shcore::Function_base_ref callback, std::shared_ptr<IConsole> console,
    std::shared_ptr<shcore::Interrupts> interrupts)
    : m_callback(std::move(callback)),
      m_console(std::move(console)),
      m_interrupts(std::move(interrupts)) {}

void Progress_callback_console::emit(const shcore::Dictionary_t &event) const {
  if (in_callback()) {
    // the callback is running on this thread and made the utility emit again;
    // handing it its own event would recurse, or wait forever for the lock
    log_debug("The progress callback emitted an event while running, dropped.");
    return;
  }

  shcore::Value result;

  try {
    shcore::Argument_list args;
    args.push_back(shcore::Value(event));

    std::lock_guard lock{m_mutex};
    m_callback_thread = std::this_thread::get_id();

    try {
      result = m_callback->invoke(args);
    } catch (...) {
      m_callback_thread = std::thread::id{};
      throw;
    }

    m_callback_thread = std::thread::id{};
  } catch (const std::exception &e) {
    log_warning("The progress callback failed: %s", e.what());
    return;
  }

  const auto cancel = (result.get_type() == shcore::Bool && result.as_bool()) ||
                      (result.get_type() == shcore::String &&
                       shcore::str_caseeq(result.get_string(), "cancel"));

  // asked once: a second interrupt makes some utilities stop at once instead
  // of finishing what they are doing
  if (cancel && !m_cancelled.exchange(true)) {
    if (m_interrupts->interrupt()) {
      log_info("The progress callback asked to stop the operation.");
    } else {
      // nothing took it: the utility has not registered its handler yet, or
      // another interrupt is being handled; the next event asks again
      log_debug("The progress callback asked to stop the operation, retrying.");
      m_cancelled = false;
    }
  }
}

void Progress_callback_console::message(
    const char *level, const std::string &text,
    const std::function<void()> &forward) const {
  if (in_callback()) {
    // printed by the callback itself (a Python print() lands on the current
    // console, which is this one): it goes where prompts go
    forward();
    return;
  }

  emit(shcore::make_dict("type", shcore::Value("message"), "level",
                         shcore::Value(level), "text",
                         shcore::Value(shcore::str_rstrip(text, "\r\n"))));
}

void Progress_callback_console::raw_print(const std::string &text,
                                          Output_stream stream,
                                          bool format_json,
                                          const Json_attributes &attribs) const {
  message(stream == Output_stream::STDERR ? "error" : "output", text, [&] {
    m_console->raw_print(text, stream, format_json, attribs);
  });
}

void Progress_callback_console::print(const std::string &text) const {
  message("output", text, [&] { m_console->print(text); });
}

void Progress_callback_console::println(const std::string &text) const {
  message("output", text, [&] { m_console->println(text); });
}

void Progress_callback_console::print_error(
    const std::string &text, const Json_attributes &attribs) const {
  message("error", text, [&] { m_console->print_error(text, attribs); });
}

void Progress_callback_console::print_warning(
    const std::string &text, const Json_attributes &attribs) const {
  message("warning", text, [&] { m_console->print_warning(text, attribs); });
}

void Progress_callback_console::print_note(
    const std::string &text, const Json_attributes &attribs) const {
  message("note", text, [&] { m_console->print_note(text, attribs); });
}

void Progress_callback_console::print_status(
    const std::string &text, const Json_attributes &attribs) const {
  message("status", text, [&] { m_console->print_status(text, attribs); });
}

void Progress_callback_console::print_info(
    const std::string &text, const Json_attributes &attribs) const {
  message("info", text, [&] { m_console->print_info(text, attribs); });
}

void Progress_callback_console::print_para(const std::string &text) const {
  message("info", text, [&] { m_console->print_para(text); });
}

void Progress_callback_console::print_value(const shcore::Value &value,
                                            const std::string &tag) const {
  message("output", value.descr(),
          [&] { m_console->print_value(value, tag); });
}

void Progress_callback_console::print_diag(const std::string &text) const {
  message("diag", text, [&] { m_console->print_diag(text); });
}

shcore::Prompt_result Progress_callback_console::prompt(
    const std::string &prompt, const shcore::prompt::Prompt_options &options,
    std::string *out_val) const {
  return m_console->prompt(prompt, options, out_val);
}

shcore::Prompt_result Progress_callback_console::prompt(
    const std::string &prompt, std::string *out_val, Validator validator,
    shcore::prompt::Prompt_type type, const std::string &title,
    const std::vector<std::string> &description,
    const std::string &default_value) const {
  return m_console->prompt(prompt, out_val, validator, type, title, description,
                           default_value);
}

Prompt_answer Progress_callback_console::confirm(
    const std::string &prompt, Prompt_answer def, const std::string &yes_label,
    const std::string &no_label, const std::string &alt_label,
    const std::string &title,
    const std::vector<std::string> &description) const {
  return m_console->confirm(prompt, def, yes_label, no_label, alt_label, title,
                            description);
}

shcore::Prompt_result Progress_callback_console::prompt_password(
    const std::string &prompt, std::string *out_val, Validator validator,
    const std::string &title,
    const std::vector<std::string> &description) const {
  return m_console->prompt_password(prompt, out_val, validator, title,
                                    description);
}

bool Progress_callback_console::select(
    const std::string &prompt_text, std::string *result,
    const std::vector<std::string> &items, size_t default_option,
    bool allow_custom, Validator validator, const std::string &title,
    const std::vector<std::string> &description) const {
  return m_console->select(prompt_text, result, items, default_option,
                           allow_custom, validator, title, description);
}

std::shared_ptr<IPager> Progress_callback_console::enable_pager() {
  return m_console->enable_pager();
}

void Progress_callback_console::enable_global_pager() {
  m_console->enable_global_pager();
}

void Progress_callback_console::disable_global_pager() {
  m_console->disable_global_pager();
}

bool Progress_callback_console::is_global_pager_enabled() const {
  return m_console->is_global_pager_enabled();
}

void Progress_callback_console::add_print_handler(
    shcore::Interpreter_print_handler *handler) {
  m_console->add_print_handler(handler);
}

void Progress_callback_console::remove_print_handler(
    shcore::Interpreter_print_handler *handler) {
  m_console->remove_print_handler(handler);
}

std::shared_ptr<Progress_callback_console> current_progress_callback() {
  auto console = current_console(true);

  while (console) {
    if (auto callback =
            std::dynamic_pointer_cast<Progress_callback_console>(console)) {
      return callback;
    }

    const auto progress =
        std::dynamic_pointer_cast<dump::Console_with_progress>(console);

    if (!progress) {
      break;
    }

    console = progress->console();
  }

  return nullptr;
}

Utility_scope::Utility_scope(const Common_options &options) {
  const auto &callback = options.progress_callback();

  if (!callback) {
    return;
  }

  // ^C reaches the shell's interrupt handling only in the thread that created
  // it, and handlers registered by any other thread are ignored; a utility
  // running elsewhere gets one of its own, which the callback can trigger
  auto interrupts = shcore::current_interrupt(true);

  if (!interrupts || !interrupts->in_creator_thread()) {
    interrupts = shcore::Interrupts::create(nullptr);
    m_interrupt.emplace(interrupts);
  }

  m_console.emplace(std::make_shared<Progress_callback_console>(
      callback, current_console(), std::move(interrupts)));
}

Utility_scope::~Utility_scope() {
  m_console.reset();

  if (m_interrupt) {
    // the helper thread of an interrupts object holds on to it, as its
    // current one, so it is not destroyed and has to be stopped
    const auto interrupts = m_interrupt->get();
    m_interrupt.reset();
    interrupts->stop_background_thread();
  }
}

}  // namespace common
}  // namespace mysqlsh
