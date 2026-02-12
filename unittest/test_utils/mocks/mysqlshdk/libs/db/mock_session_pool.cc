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

#include "unittest/test_utils/mocks/mysqlshdk/libs/db/mock_session_pool.h"

#include <memory>
#include <utility>

#include "mysqlshdk/libs/db/connection_options.h"
#include "unittest/test_utils/mocks/gmock_clean.h"

namespace testing {

Mock_session_pool::Mock_session_pool()
    : mysqlshdk::db::Session_pool(mysqlshdk::db::Connection_options(), 1) {
  // Set up default behaviors
  ON_CALL(*this, max_size()).WillByDefault(Return(1));
  ON_CALL(*this, available_count()).WillByDefault(Return(1));
}

Mock_session_pool::Mock_session_pool(size_t max_size)
    : mysqlshdk::db::Session_pool(mysqlshdk::db::Connection_options(),
                                  max_size) {
  // Set up default behaviors
  ON_CALL(*this, max_size()).WillByDefault(Return(max_size));
  ON_CALL(*this, available_count()).WillByDefault(Return(0));
}

void Mock_session_pool::setup_session_sequence(
    const std::vector<std::shared_ptr<mysqlshdk::db::ISession>> &sessions) {
  m_session_sequence.assign(sessions.begin(), sessions.end());

  // Set up expectations to return sessions from the sequence
  for (const auto &session : sessions) {
    EXPECT_CALL(*this, acquire(false))
        .WillOnce(Return(session))
        .RetiresOnSaturation();
  }
}

void Mock_session_pool::setup_repeated_session(
    std::shared_ptr<mysqlshdk::db::ISession> session) {
  m_repeated_session = std::move(session);

  // Set up default behavior to always return the same session
  ON_CALL(*this, acquire(false)).WillByDefault(Return(m_repeated_session));
}

void Mock_session_pool::expect_acquire_calls(size_t count) {
  if (count == 0) {
    EXPECT_CALL(*this, acquire(false)).Times(0);
  } else {
    EXPECT_CALL(*this, acquire(false)).Times(count);
  }
}

void Mock_session_pool::expect_release_calls(
    const std::vector<std::shared_ptr<mysqlshdk::db::ISession>> &sessions) {
  for (const auto &session : sessions) {
    EXPECT_CALL(*this, release(session)).Times(1);
  }
}

}  // namespace testing
