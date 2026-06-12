/*
 * Copyright (c) 2019, 2026, Oracle and/or its affiliates.
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

#include "mysqlshdk/libs/utils/utils_file.h"
#include "mysqlshdk/libs/utils/utils_general.h"
#include "mysqlshdk/libs/utils/utils_path.h"

#include <cerrno>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif  // _WIN32

namespace shcore {
namespace test {

#ifndef _WIN32
class Scoped_umask {
 public:
  explicit Scoped_umask(mode_t mask) : m_previous{umask(mask)} {}
  ~Scoped_umask() { umask(m_previous); }

 private:
  mode_t m_previous;
};
#endif  // !_WIN32

#ifdef _WIN32
static constexpr DWORD k_allow_unprivileged_create =
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
    SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#else
    0x2;
#endif  // SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE

bool create_file_symlink(const std::string &link, const std::string &target,
                         DWORD *error) {
  const auto wide_link = utf8_to_wide(link);
  const auto wide_target = utf8_to_wide(target);

  if (CreateSymbolicLinkW(wide_link.c_str(), wide_target.c_str(),
                          k_allow_unprivileged_create)) {
    return true;
  }

  *error = GetLastError();
  if (ERROR_INVALID_PARAMETER != *error) {
    return false;
  }

  if (CreateSymbolicLinkW(wide_link.c_str(), wide_target.c_str(), 0)) {
    return true;
  }

  *error = GetLastError();
  return false;
}
#endif  // _WIN32

class utils_file : public ::testing::Test {
 protected:
  static void SetUpTestCase() {
    s_test_folder = path::join_path(getenv("TMPDIR"), "test_utils_file");
  }

  void SetUp() override { create_directory(s_test_folder); }

  void TearDown() override { remove_directory(s_test_folder, false); }

  static std::string s_test_folder;
};

std::string utils_file::s_test_folder;

TEST_F(utils_file, exists) {
  {
    const auto directory = path::join_path(s_test_folder, "1");
    create_directory(directory);
    EXPECT_TRUE(path_exists(directory));
    EXPECT_TRUE(is_folder(directory));
    EXPECT_FALSE(is_file(directory));
    EXPECT_NO_THROW(remove_directory(directory, false));
    ASSERT_FALSE(path_exists(directory));
  }

  {
    const auto directory = s_test_folder + "\\zażółć_gęślą_jaźń";
    create_directory(directory);
    EXPECT_TRUE(path_exists(directory));
    EXPECT_TRUE(is_folder(directory));
    EXPECT_FALSE(is_file(directory));
    EXPECT_NO_THROW(remove_directory(directory, false));
    ASSERT_FALSE(path_exists(directory));
  }

  {
    const auto file = path::join_path(s_test_folder, "2.txt");
    EXPECT_TRUE(create_file(file, ""));
    EXPECT_TRUE(path_exists(file));
    EXPECT_FALSE(is_folder(file));
    EXPECT_TRUE(is_file(file));
    EXPECT_NO_THROW(delete_file(file, false));
    EXPECT_FALSE(path_exists(file));
  }

  {
    const auto file = path::join_path(s_test_folder, "zażółć_gęślą_jaźń.txt");
    EXPECT_TRUE(
        create_file(file, "Pchnąć w tę łódź jeża lub ośm skrzyń fig", true));
    EXPECT_TRUE(path_exists(file));
    EXPECT_FALSE(is_folder(file));
    EXPECT_TRUE(is_file(file));
    EXPECT_NO_THROW(delete_file(file, false));
    EXPECT_FALSE(path_exists(file));
  }

  {
    const auto file =
        path::join_path(s_test_folder, "ZAŻÓŁĆ_GĘŚLĄ_JAŹŃ_CAPITAL.txt");
    EXPECT_TRUE(create_file(file, "Pchnąć w tę łódź jeża lub ośm skrzyń fig"));
    EXPECT_TRUE(path_exists(file));
    EXPECT_FALSE(is_folder(file));
    EXPECT_TRUE(is_file(file));
    EXPECT_NO_THROW(delete_file(file, false));
    EXPECT_FALSE(path_exists(file));
  }

  {
    const auto missing = path::join_path(s_test_folder, "missing");
    EXPECT_FALSE(path_exists(missing));
    EXPECT_FALSE(is_folder(missing));
    EXPECT_FALSE(is_file(missing));
  }
}

TEST_F(utils_file, create_directory) {
  {
    const auto dir = path::join_path(s_test_folder, "foo");
    ASSERT_FALSE(path_exists(dir));
    EXPECT_NO_THROW(create_directory(path::join_path(dir, "bar/baz"), true));
    ASSERT_TRUE(path_exists(dir));
    EXPECT_NO_THROW(remove_directory(dir, true));
    ASSERT_FALSE(path_exists(dir));
  }

  {
    const auto dir = path::join_path(s_test_folder, "Zażółć");
    ASSERT_FALSE(path_exists(dir));
    EXPECT_NO_THROW(create_directory(
        path::join_path(dir, "gęślą/jaźń/ZAŻÓŁĆ/ÓŚM/SKRZYŃ/x/y/z/a/b/c"),
        true));
    ASSERT_TRUE(path_exists(dir));
    EXPECT_NO_THROW(remove_directory(dir, true));
    ASSERT_FALSE(path_exists(dir));
  }
}

TEST_F(utils_file, create_private_file) {
  const auto file = path::join_path(s_test_folder, "private");

  FILE *private_file = create_private_file(file);
  ASSERT_NE(nullptr, private_file);
  EXPECT_EQ(0, fclose(private_file));

  EXPECT_TRUE(is_file(file));

#ifndef _WIN32
  struct stat status;
  ASSERT_EQ(0, stat(file.c_str(), &status));
  EXPECT_EQ(S_IRUSR | S_IWUSR, status.st_mode & 0777);
#endif  // !_WIN32

  EXPECT_THROW(create_private_file(file), std::runtime_error);

  delete_file(file);
}

#ifdef _WIN32
TEST_F(utils_file, create_private_file_sets_private_access_rights) {
  const auto file = path::join_path(s_test_folder, "private-permissions");
  const auto cleanup = on_leave_scope([&file]() { delete_file(file, true); });

  FILE *private_file = create_private_file(file);
  ASSERT_NE(nullptr, private_file);
  EXPECT_EQ(0, fclose(private_file));

  EXPECT_NO_THROW(check_file_access_rights_to_open(file));
}
#endif  // _WIN32

#ifndef _WIN32
TEST_F(utils_file, create_private_file_does_not_follow_symlink) {
  const auto target = path::join_path(s_test_folder, "target");
  const auto link = path::join_path(s_test_folder, "link");

  ASSERT_TRUE(create_file(target, "safe"));
  ASSERT_EQ(0, symlink(target.c_str(), link.c_str()));

  FILE *private_file = nullptr;
  EXPECT_THROW(private_file = create_private_file(link), std::runtime_error);
  if (private_file) {
    fclose(private_file);
  }

  EXPECT_EQ("safe", get_text_file(target));

  delete_file(link);
  delete_file(target);
}
#endif  // !_WIN32

#ifdef _WIN32
TEST_F(utils_file, create_private_file_does_not_follow_symlink) {
  const auto target = path::join_path(s_test_folder, "target");
  const auto link = path::join_path(s_test_folder, "link");

  DWORD symlink_error = 0;
  if (!create_file_symlink(link, target, &symlink_error)) {
    GTEST_SKIP() << "Unable to create Windows symlink: " << symlink_error;
  }

  const auto cleanup = on_leave_scope([&link, &target]() {
    delete_file(link, true);
    delete_file(target, true);
  });

  FILE *private_file = nullptr;
  EXPECT_THROW(private_file = create_private_file(link), std::runtime_error);
  if (private_file) {
    fclose(private_file);
  }

  EXPECT_FALSE(path_exists(target));
}
#endif  // _WIN32

TEST_F(utils_file, create_temporary_folder) {
#ifndef _WIN32
  errno = EEXIST;
#endif  // !_WIN32

  const auto folder = create_temporary_folder();

  EXPECT_TRUE(is_folder(folder));

#ifndef _WIN32
  struct stat status;
  ASSERT_EQ(0, stat(folder.c_str(), &status));
  EXPECT_EQ(S_IRWXU, status.st_mode & 0777);
#endif  // !_WIN32

  ASSERT_TRUE(create_file(path::join_path(folder, "file"), "content"));
  EXPECT_NO_THROW(remove_directory(folder, true));
  EXPECT_FALSE(path_exists(folder));
}

#ifndef _WIN32
TEST_F(utils_file, private_file_and_temporary_folder_ignore_restrictive_umask) {
  const Scoped_umask restrictive_umask{0777};
  struct stat status;

  const auto file = path::join_path(s_test_folder, "private-umask");
  auto file_cleanup = on_leave_scope([&file]() {
    if (path_exists(file)) {
      chmod(file.c_str(), S_IRUSR | S_IWUSR);
      delete_file(file);
    }
  });

  FILE *private_file = create_private_file(file);
  ASSERT_NE(nullptr, private_file);
  EXPECT_EQ(0, fclose(private_file));

  ASSERT_EQ(0, stat(file.c_str(), &status));
  EXPECT_EQ(S_IRUSR | S_IWUSR, status.st_mode & 0777);

  const auto folder = create_temporary_folder();
  auto folder_cleanup = on_leave_scope([&folder]() {
    if (path_exists(folder)) {
      chmod(folder.c_str(), S_IRWXU);
      remove_directory(folder, true);
    }
  });

  ASSERT_EQ(0, stat(folder.c_str(), &status));
  EXPECT_EQ(S_IRWXU, status.st_mode & 0777);
}
#endif  // !_WIN32

}  // namespace test
}  // namespace shcore
