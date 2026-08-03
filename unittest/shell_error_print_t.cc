/*
 * Copyright (c) 2017, 2024, Oracle and/or its affiliates.
 * Copyright (c) 2026, MariaDB Corporation.
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

#include "unittest/gtest_clean.h"
#include "unittest/test_utils.h"

#include <string>

#include "mysqlshdk/libs/textui/textui.h"

namespace mysqlsh {

class Shell_error_printing : public Shell_core_test_wrapper {};

TEST_F(Shell_error_printing, print_error) {
  reset_shell();
  wipe_all();

  current_console()->print_error("test");
  EXPECT_EQ("ERROR: test\n", output_handler.std_out);
  wipe_all();
}

TEST_F(Shell_error_printing, print_diag) {
  reset_shell();
  wipe_all();
  current_console()->print_diag("test");
  EXPECT_EQ("test", output_handler.std_err);
  wipe_all();
  current_console()->print_diag("test\n");
  EXPECT_EQ("test\n", output_handler.std_err);
}

TEST_F(Shell_error_printing, sanitizes_terminal_controls) {
  reset_shell();
  wipe_all();

  const std::string text =
      "server\x1B]52;c;AAAA\x07"
      "after";

  current_console()->print_error(text);
  EXPECT_EQ("ERROR: server\\x1B]52;c;AAAA\\x07after\n", output_handler.std_out);
  wipe_all();

  current_console()->print_warning(text);
  EXPECT_EQ("WARNING: server\\x1B]52;c;AAAA\\x07after\n",
            output_handler.std_out);
  wipe_all();

  current_console()->print_diag(text);
  EXPECT_EQ("server\\x1B]52;c;AAAA\\x07after", output_handler.std_err);
}

TEST_F(Shell_error_printing, preserves_text_decorations) {
  reset_shell();
  wipe_all();

  struct Color_capability_guard {
    explicit Color_capability_guard(mysqlshdk::textui::Color_capability cap)
        : original(mysqlshdk::textui::get_color_capability()) {
      mysqlshdk::textui::set_color_capability(cap);
    }

    ~Color_capability_guard() {
      mysqlshdk::textui::set_color_capability(original);
    }

    mysqlshdk::textui::Color_capability original;
  };

  const Color_capability_guard guard{mysqlshdk::textui::Color_16};
  const auto decorated = mysqlshdk::textui::bold("decorated");

  current_console()->print_info(decorated);
  EXPECT_EQ(decorated + "\n", output_handler.std_out);
}

TEST_F(Shell_error_printing, python_stack) {
  reset_shell();
  execute("\\py");
  execute("import sys");
  wipe_all();

  execute("sys.stderr.write('hello world\\n')");
  execute("sys.stderr.write('foo')");
  EXPECT_EQ("hello world\nfoo", output_handler.std_err);

  wipe_all();
  execute("1/0");
  EXPECT_EQ(
      "Traceback (most recent call last):\n"
      "  File \"<string>\", line 1, in <module>\n"
      "ZeroDivisionError: division by zero\n",
      output_handler.std_err);

  wipe_all();
  execute("raise KeyboardInterrupt()");
  EXPECT_EQ(
      "Traceback (most recent call last):\n"
      "  File \"<string>\", line 1, in <module>\n"
      "KeyboardInterrupt\n",
      output_handler.std_err);

  wipe_all();
  execute("raise BaseException()");
  EXPECT_EQ(
      "Traceback (most recent call last):\n"
      "  File \"<string>\", line 1, in <module>\n"
      "BaseException\n",
      output_handler.std_err);

#ifdef HAVE_ADMIN_API
  wipe_all();
  execute("dba.deploy_sandbox_instance(-1, {'password':''})");
  EXPECT_EQ(
      "Traceback (most recent call last):\n"
      "  File \"<string>\", line 1, in <module>\n"
      "ValueError: "
      "Invalid value for 'port': Please use a valid TCP port number >= 1024 "
      "and <= 65535\n\n",
      output_handler.std_err);
#endif
}

#ifdef HAVE_JS
TEST_F(Shell_error_printing, js_stack) {
  _opts->set_interactive(false);
  reset_shell();
  execute("\\js");
  wipe_all();
  execute("foo.bar");
  EXPECT_EQ(
      R"(foo is not defined (ReferenceError)
 at (shell):1
)",
      output_handler.std_err);

  wipe_all();
  execute("throw 'SomethingWrong'");
  EXPECT_EQ(
      R"(SomethingWrong at (shell):1
)",
      output_handler.std_err);

  wipe_all();
  execute("dba.deploySandboxInstance(-1, {'password':''})");
  EXPECT_EQ(
      R"(Invalid value for 'port': Please use a valid TCP port number >= 1024 and <= 65535 (ArgumentError)
 at (shell):1
)",
      output_handler.std_err);
}
#endif

TEST_F(Shell_error_printing, sql_error) {
  execute("\\sql");
  execute("\\connect " + _mysql_uri);
  wipe_all();

  std::string error = shcore::str_format(
      "You have an error in your SQL syntax; check the manual "
      "that corresponds to your %s server version for the right syntax to use "
      "near 'garbage' at line 1\n",
      server_vendor.c_str());

  execute("garbage;");
  MY_EXPECT_OUTPUT_CONTAINS(error.c_str(), output_handler.std_err);
#if HAVE_JS
  execute("\\js");
#else
  execute("\\py");
#endif
  execute("session.close();");
}

}  // namespace mysqlsh
