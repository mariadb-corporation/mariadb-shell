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

#ifndef MYSQLSHDK_LIBS_DB_SESSION_POOL_H_
#define MYSQLSHDK_LIBS_DB_SESSION_POOL_H_

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

#include "mysqlshdk/libs/db/connection_options.h"
#include "mysqlshdk/libs/db/session.h"

namespace mysqlshdk {
namespace db {

class SHCORE_PUBLIC Session_pool {
 public:
  /**
   * Constructs a session pool.
   *
   * @param connection_options The connection options to use for creating new
   * sessions.
   * @param max_size The maximum number of sessions in the pool. 0 means
   * unlimited.
   */
  Session_pool(const Connection_options &connection_options,
               size_t max_size = 0);

  Session_pool(const Session_pool &) = delete;
  Session_pool(Session_pool &&) = delete;

  Session_pool &operator=(const Session_pool &) = delete;
  Session_pool &operator=(Session_pool &&) = delete;

  virtual ~Session_pool() = default;

  /**
   * Acquires a session from the pool.
   *
   * If no session is available and the pool has not reached its maximum size,
   * a new session will be created. If the pool is at maximum size, this method
   * will block until a session becomes available.
   *
   * @param dont_block If true, the method will not block if no session is
   * available and instead will open a new one.
   *
   * @return A shared pointer to an ISession.
   */
  virtual std::shared_ptr<ISession> acquire(bool dont_block = false);

  /**
   * Releases a session back to the pool.
   *
   * @param session The session to release. Must have been acquired from this
   * pool.
   */
  virtual void release(std::shared_ptr<ISession> session);

  /**
   * Gets the current number of available sessions in the pool.
   *
   * @return The number of available sessions.
   */
  virtual size_t available_count() const;

  /**
   * Gets the maximum number of sessions allowed in the pool.
   *
   * @return The maximum number of sessions. 0 means unlimited.
   */
  virtual size_t max_size() const;

 private:
  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  std::deque<std::shared_ptr<ISession>> m_available_sessions;
  Connection_options m_connection_options;
  size_t m_max_size;
  size_t m_active_count = 0;
};

}  // namespace db
}  // namespace mysqlshdk

#endif  // MYSQLSHDK_LIBS_DB_SESSION_POOL_H_
