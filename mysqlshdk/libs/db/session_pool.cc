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

#include "mysqlshdk/libs/db/session_pool.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <utility>

#include "mysqlshdk/libs/db/mysql/session.h"
#ifdef HAVE_X_PROTOCOL
#include "mysqlshdk/libs/db/mysqlx/session.h"
#endif

namespace mysqlshdk {
namespace db {

namespace {

std::shared_ptr<ISession> create_session(const Connection_options &options) {
  std::shared_ptr<ISession> session;

  switch (options.get_session_type()) {
#ifdef HAVE_X_PROTOCOL
    case mysqlsh::SessionType::X:
      session = mysqlx::Session::create();
      break;
#endif
    case mysqlsh::SessionType::Classic:
      session = mysql::Session::create();
      break;
    default:
      throw std::logic_error("Invalid session type for session pool");
  }

  session->connect(options);

  session->execute("SET SQL_MODE=DEFAULT");

  return session;
}

}  // namespace

Session_pool::Session_pool(const Connection_options &connection_options,
                           size_t max_size)
    : m_connection_options(connection_options), m_max_size(max_size) {}

std::shared_ptr<ISession> Session_pool::acquire(bool dont_block) {
  std::unique_lock<std::mutex> lock(m_mutex);

  const auto get_available_session = [this]() -> std::shared_ptr<ISession> {
    auto session = std::move(m_available_sessions.front());
    m_available_sessions.pop_front();
    ++m_active_count;
    return session;
  };

  const auto get_new_session = [this, &lock]() -> std::shared_ptr<ISession> {
    ++m_active_count;
    lock.unlock();  // Unlock before creating session
    try {
      return create_session(m_connection_options);
    } catch (...) {
      // If creation failed, decrement active count
      std::lock_guard<std::mutex> relock(m_mutex);
      --m_active_count;
      throw;
    }
  };

  // If there are available sessions, return one
  if (!m_available_sessions.empty()) {
    return get_available_session();
  }

  // If we haven't reached max size (or dont_block=true), create a new session
  if (dont_block || m_max_size == 0 || m_active_count < m_max_size) {
    return get_new_session();
  }
  assert(lock.owns_lock());
  // Wait for a session to become available
  m_cv.wait(lock, [this]() {
    return !m_available_sessions.empty() ||
           (m_max_size > 0 && m_active_count < m_max_size);
  });

  // After waiting, we should have a session available or can create one
  if (!m_available_sessions.empty()) {
    return get_available_session();
  }

  // Create new session
  return get_new_session();
}

void Session_pool::release(std::shared_ptr<ISession> session) {
  if (!session) {
    return;
  }

  std::unique_lock<std::mutex> lock(m_mutex);
  if (session->is_open()) {
    m_available_sessions.push_back(std::move(session));
  }
  --m_active_count;
  lock.unlock();
  m_cv.notify_one();
}

size_t Session_pool::available_count() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_available_sessions.size();
}

size_t Session_pool::max_size() const { return m_max_size; }

}  // namespace db
}  // namespace mysqlshdk
