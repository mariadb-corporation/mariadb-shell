/*
 * Copyright (c) 2026, Oracle and/or its affiliates.
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

#ifndef UNITTEST_MOCKS_MYSQLSHDK_LIBS_DB_MOCK_SESSION_POOL_H_
#define UNITTEST_MOCKS_MYSQLSHDK_LIBS_DB_MOCK_SESSION_POOL_H_

#include <deque>
#include <memory>
#include <mutex>

#include "mysqlshdk/libs/db/session_pool.h"
#include "unittest/test_utils/mocks/gmock_clean.h"

namespace testing {

/**
 * Mock for a Session_pool object
 *
 * Simple call expectations and return values can be defined with:
 *
 * EXPECT_CALL(pool, acquire()).WillOnce(Return(mock_session));
 *
 * Where:
 *   - First parameter is this pool object
 *   - Second parameter is the function and parameters that is expected to be
 * called
 *   - After closing the EXPECT_CALL() some actions can be defined to
 * return specific results
 *
 * Keep in mind that the returned data must match the return type of the
 * function called.
 */
class Mock_session_pool : public mysqlshdk::db::Session_pool {
 public:
  Mock_session_pool();
  explicit Mock_session_pool(size_t max_size);

  // Mock methods for Session_pool interface
  MOCK_METHOD1(acquire, std::shared_ptr<mysqlshdk::db::ISession>(bool));
  MOCK_METHOD1(release, void(std::shared_ptr<mysqlshdk::db::ISession>));
  MOCK_CONST_METHOD0(available_count, size_t());
  MOCK_CONST_METHOD0(max_size, size_t());

  /**
   * Helper method to set up a pool that returns a sequence of sessions
   * from acquire() calls.
   */
  void setup_session_sequence(
      const std::vector<std::shared_ptr<mysqlshdk::db::ISession>> &sessions);

  /**
   * Helper method to set up a pool that always returns the same session.
   */
  void setup_repeated_session(std::shared_ptr<mysqlshdk::db::ISession> session);

  /**
   * Helper method to expect acquire() to be called a specific number of times.
   */
  void expect_acquire_calls(size_t count);

  /**
   * Helper method to expect release() to be called with specific sessions.
   */
  void expect_release_calls(
      const std::vector<std::shared_ptr<mysqlshdk::db::ISession>> &sessions);

 private:
  // Store sessions for sequence-based mocking
  std::deque<std::shared_ptr<mysqlshdk::db::ISession>> m_session_sequence;
  std::shared_ptr<mysqlshdk::db::ISession> m_repeated_session;
};

}  // namespace testing

#endif  // UNITTEST_MOCKS_MYSQLSHDK_LIBS_DB_MOCK_SESSION_POOL_H_
