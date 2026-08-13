/*
 * Copyright (c) 2026, Oracle and/or its affiliates.
 * Copyright (c) 2026, MariaDB plc.
 *
 * SPDX-License-Identifier: GPL-2.0-only
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

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "modules/adminapi/common/provisioning_interface.h"
#include "mysqlshdk/include/shellcore/scoped_contexts.h"
#include "mysqlshdk/include/shellcore/shell_options.h"
#include "mysqlshdk/libs/utils/utils_file.h"
#include "mysqlshdk/libs/utils/utils_path.h"
#include "mysqlshdk/libs/utils/utils_string.h"
#include "mysqlshdk/shellcore/shell_console.h"
#include "unittest/test_utils.h"
#include "unittest/test_utils/cleanup.h"

extern const char *g_mysqlsh_path;

namespace mysqlsh::dba {

namespace {

std::shared_ptr<Shell_options> make_shell_options(
    const std::string &options_file, std::vector<std::string> args) {
  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (auto &arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);

  return std::make_shared<Shell_options>(static_cast<int>(args.size()),
                                         argv.data(), options_file,
                                         Shell_options::Option_flags_set());
}

std::string current_log_level_arg() {
  return "--log-level=" + std::to_string(static_cast<int>(
                              shcore::current_logger()->get_log_level()));
}

}  // namespace

TEST(ProvisioningInterface_test, forwards_plugin_disable_options) {
#ifdef _WIN32
  SKIP_TEST("This test uses a POSIX shell script as fake mysqlsh");
#else
  const auto test_dir = shcore::create_temporary_folder();
  tests::Cleanup cleanup;
  cleanup.add([test_dir]() { shcore::remove_directory(test_dir); });

  const auto argv_file = shcore::path::join_path(test_dir, "argv.txt");
  cleanup += tests::Cleanup::set_env_var("MYSQLSH_TEST_ARGV_FILE", argv_file);

  const auto fake_mysqlsh = shcore::path::join_path(test_dir, "mysqlsh");

  // Record the child Shell argv and return a valid mysqlprovision response
  cleanup += tests::Cleanup::write_file(fake_mysqlsh, R"(#!/usr/bin/env sh
: > "$MYSQLSH_TEST_ARGV_FILE"
for arg in "$@"; do
  printf '%s\n' "$arg" >> "$MYSQLSH_TEST_ARGV_FILE"
done
while IFS= read -r line; do
  [ "$line" = "." ] && break
done
printf '{"type":"INFO","msg":"ok"}\n'
)");
  ASSERT_EQ(0, shcore::ch_mod(fake_mysqlsh, 0700));

  const char *previous_mysqlsh_path = g_mysqlsh_path;
  g_mysqlsh_path = fake_mysqlsh.c_str();
  tests::Cleanup restore_mysqlsh_path;
  restore_mysqlsh_path.add(
      [previous_mysqlsh_path]() { g_mysqlsh_path = previous_mysqlsh_path; });

  Shell_test_output_handler output_handler;
  Scoped_console scoped_console{
      std::make_shared<Shell_console>(&output_handler.deleg)};

  const auto run_start_sandbox =
      [&](std::vector<std::string> shell_args) -> std::vector<std::string> {
    auto options =
        make_shell_options(shcore::path::join_path(test_dir, "options.json"),
                           std::move(shell_args));
    Scoped_shell_options scoped_options(options);

    shcore::Value::Array_type_ref errors;
    ProvisioningInterface provisioning;
    EXPECT_EQ(0, provisioning.start_sandbox(33060, test_dir, &errors));

    auto argv = shcore::get_text_file(argv_file);
    if (!argv.empty() && argv.back() == '\n') argv.pop_back();
    return shcore::str_split(argv, "\n");
  };

  // Plugin flags must be before --pym so they are parsed as Shell options
  EXPECT_EQ((std::vector<std::string>{current_log_level_arg(), "--pym",
                                      "mysql_gadgets", "sandbox"}),
            run_start_sandbox({"ut"}));
  EXPECT_EQ(
      (std::vector<std::string>{current_log_level_arg(), "--disable-plugins",
                                "--disable-builtin-plugins", "--pym",
                                "mysql_gadgets", "sandbox"}),
      run_start_sandbox(
          {"ut", "--disable-plugins", "--disable-builtin-plugins"}));
#endif
}

}  // namespace mysqlsh::dba
