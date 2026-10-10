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

#ifndef MODULES_UTIL_COMMON_PROGRESS_CALLBACK_H_
#define MODULES_UTIL_COMMON_PROGRESS_CALLBACK_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "mysqlshdk/include/scripting/types.h"
#include "mysqlshdk/include/shellcore/console.h"
#include "mysqlshdk/include/shellcore/interrupt_handler.h"
#include "mysqlshdk/include/shellcore/scoped_contexts.h"
#include "mysqlshdk/libs/utils/mysys_thread.h"

namespace mysqlsh {
namespace common {

class Common_options;

/**
 * The console a utility prints to while it runs with the `progressCallback`
 * option. What the utility would print, and the progress of its stages, is
 * handed to the callback as one dictionary per event, with a `type`:
 *
 *  - "message": `level` (output, info, status, note, warning, error or diag)
 *    and `text`.
 *  - "stageStarted": `stage`, the stage's description.
 *  - "progress": `stage`, plus `current` and `total`, and for a stage that
 *    measures throughput `throughput` (items per second), `etaSeconds`,
 *    `items` and `totalIsApproximate`, or for one that counts `totalKnown`.
 *  - "stageFinished": `stage` and `seconds`.
 *
 * The callback stops the utility by returning "cancel" or true: it then stops
 * as it does on ^C. That works from any thread, where ^C reaches only a
 * utility running on the main thread.
 *
 * Prompts and the pager go to the console this one replaced.
 */
class Progress_callback_console final : public IConsole {
 public:
  Progress_callback_console(shcore::Function_base_ref callback,
                            std::shared_ptr<IConsole> console,
                            std::shared_ptr<shcore::Interrupts> interrupts);

  Progress_callback_console(const Progress_callback_console &) = delete;
  Progress_callback_console(Progress_callback_console &&) = delete;

  Progress_callback_console &operator=(const Progress_callback_console &) =
      delete;
  Progress_callback_console &operator=(Progress_callback_console &&) = delete;

  ~Progress_callback_console() override = default;

  /**
   * Hands an event to the callback, one at a time whichever thread it comes
   * from. A failing callback is logged and does not stop the utility.
   */
  void emit(const shcore::Dictionary_t &event) const;

  bool use_json() const override { return false; }

  void raw_print(const std::string &text, Output_stream stream,
                 bool format_json = true,
                 const Json_attributes &attribs = {}) const override;
  void print(const std::string &text) const override;
  void println(const std::string &text = "") const override;
  void print_error(const std::string &text,
                   const Json_attributes &attribs = {}) const override;
  void print_warning(const std::string &text,
                     const Json_attributes &attribs = {}) const override;
  void print_note(const std::string &text,
                  const Json_attributes &attribs = {}) const override;
  void print_status(const std::string &text,
                    const Json_attributes &attribs = {}) const override;
  void print_info(const std::string &text = "",
                  const Json_attributes &attribs = {}) const override;
  void print_para(const std::string &text) const override;
  void print_value(const shcore::Value &value,
                   const std::string &tag) const override;
  void print_diag(const std::string &text) const override;

  shcore::Prompt_result prompt(const std::string &prompt,
                               const shcore::prompt::Prompt_options &options,
                               std::string *out_val) const override;
  shcore::Prompt_result prompt(
      const std::string &prompt, std::string *out_val,
      Validator validator = nullptr,
      shcore::prompt::Prompt_type type = shcore::prompt::Prompt_type::TEXT,
      const std::string &title = "",
      const std::vector<std::string> &description = {},
      const std::string &default_value = "") const override;
  Prompt_answer confirm(
      const std::string &prompt, Prompt_answer def = Prompt_answer::NO,
      const std::string &yes_label = "&Yes",
      const std::string &no_label = "&No", const std::string &alt_label = "",
      const std::string &title = "",
      const std::vector<std::string> &description = {}) const override;
  shcore::Prompt_result prompt_password(
      const std::string &prompt, std::string *out_val,
      Validator validator = nullptr, const std::string &title = "",
      const std::vector<std::string> &description = {}) const override;
  bool select(const std::string &prompt_text, std::string *result,
              const std::vector<std::string> &items, size_t default_option = 0,
              bool allow_custom = false, Validator validator = nullptr,
              const std::string &title = "",
              const std::vector<std::string> &description = {}) const override;

  std::shared_ptr<IPager> enable_pager() override;
  void enable_global_pager() override;
  void disable_global_pager() override;
  bool is_global_pager_enabled() const override;

  void add_print_handler(shcore::Interpreter_print_handler *handler) override;
  void remove_print_handler(
      shcore::Interpreter_print_handler *handler) override;

 private:
  void message(const char *level, const std::string &text) const;

  shcore::Function_base_ref m_callback;
  std::shared_ptr<IConsole> m_console;
  std::shared_ptr<shcore::Interrupts> m_interrupts;
  mutable std::mutex m_mutex;
  mutable std::atomic<bool> m_cancelled = false;
};

/**
 * The callback console the current thread's utility prints to, if it was
 * given a `progressCallback`; looks through the console the progress display
 * puts in front of it.
 */
std::shared_ptr<Progress_callback_console> current_progress_callback();

/**
 * What a utility needs around its run to work from any thread:
 *
 *  - mysys thread state, which a thread a scripting language created lacks;
 *  - with a `progressCallback`, the console that hands output and progress to
 *    it, and interrupt handling of the calling thread's own where the shell's
 *    is another thread's, so that the callback can stop the utility.
 *
 * Created before the utility's progress display and interrupt handler, and
 * outlives both.
 */
class Utility_scope final {
 public:
  explicit Utility_scope(const Common_options &options);

  Utility_scope(const Utility_scope &) = delete;
  Utility_scope(Utility_scope &&) = delete;

  Utility_scope &operator=(const Utility_scope &) = delete;
  Utility_scope &operator=(Utility_scope &&) = delete;

  ~Utility_scope();

 private:
  mysqlshdk::utils::Mysys_thread_guard m_mysys;
  std::optional<Scoped_interrupt> m_interrupt;
  std::optional<Scoped_console> m_console;
};

}  // namespace common
}  // namespace mysqlsh

#endif  // MODULES_UTIL_COMMON_PROGRESS_CALLBACK_H_
