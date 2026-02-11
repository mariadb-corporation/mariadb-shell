/*
 * Copyright (c) 2021, 2026, Oracle and/or its affiliates.
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

#ifndef MYSQLSHDK_LIBS_UTILS_THREAD_POOL_H_
#define MYSQLSHDK_LIBS_UTILS_THREAD_POOL_H_

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "mysqlshdk/include/shellcore/scoped_contexts.h"
#include "mysqlshdk/libs/utils/atomic_flag.h"
#include "mysqlshdk/libs/utils/synchronized_queue.h"

namespace shcore {

/**
 * A pool of threads which allows to execute an operation in a pool, and
 * process the result of that operation in the calling thread.
 */
template <typename T>
class Thread_pool_base final {
 public:
  using Priority = Queue_priority;
  using Producer = std::function<T()>;
  using Processor = std::function<void(T &&)>;

  enum class Async_state {
    IDLE,
    PRODUCING,
    PROCESSING,
    DONE,
    TERMINATED,
  };

  Thread_pool_base() = delete;

  /**
   * Sets the number of threads the pool is going to use.
   *
   * @param threads Number of threads to use.
   * @param interrupted Flag to signalize an interrupt.
   */
  explicit Thread_pool_base(uint64_t threads,
                            shcore::atomic_flag *interrupted = nullptr)
      : m_threads(threads),
        m_active_threads(threads),
        m_workers(threads),
        m_worker_exceptions(threads),
        m_interrupted(interrupted) {}

  Thread_pool_base(uint64_t threads, uint64_t max_queued_tasks,
                   const std::function<void()> &thread_init,
                   const std::function<void()> &thread_finish,
                   shcore::atomic_flag *interrupted = nullptr)
      : m_threads(threads),
        m_active_threads(threads),
        m_workers(threads),
        m_worker_exceptions(threads),
        m_interrupted(interrupted),
        m_worker_tasks(max_queued_tasks),
        m_thread_init(thread_init),
        m_thread_finish(thread_finish) {}

  Thread_pool_base(const Thread_pool_base &) = delete;
  Thread_pool_base(Thread_pool_base &&) = delete;

  Thread_pool_base &operator=(const Thread_pool_base &) = delete;
  Thread_pool_base &operator=(Thread_pool_base &&) = delete;

  ~Thread_pool_base() {
    kill_threads();
    wait_for_async_thread();
  }

  /**
   * Initializes and starts the threads.
   */
  void start_threads() {
    for (auto i = decltype(m_threads){0}; i < m_threads; ++i) {
      m_workers[i] = mysqlsh::spawn_scoped_thread(
          [this](auto id) {
            if (m_thread_init) {
              m_thread_init();
            }
            try {
              while (true) {
                auto task = m_worker_tasks.pop();

                if (interrupted()) {
                  break;
                }

                if (!task.produce_data) {
                  break;
                }

                auto data = task.produce_data();

                m_main_thread_tasks.push(
                    [data = std::move(data),
                     process_data = std::move(task.process_data)]() mutable {
                      process_data(std::move(data));
                    });

                if (interrupted()) {
                  break;
                }
              }

              maybe_shutdown_main_thread();
            } catch (...) {
              m_worker_exceptions[id] = std::current_exception();
              emergency_shutdown();
            }
            if (m_thread_finish) {
              m_thread_finish();
            }
          },
          i);
    }
  }

  /**
   * Adds a task to be executed.
   *
   * @param produce_data Operation which is going to be executed in the thread
   *        pool.
   * @param process_data Operation which is going to be executed in the calling
   *        thread using the result of produce_data.
   *
   * @throws logic_error If called after a call to tasks_done().
   */
  void add_task(Producer &&produce_data, Processor &&process_data,
                Priority priority = Priority::MEDIUM) {
    if (!m_all_tasks_pushed) {
      m_worker_tasks.push({std::move(produce_data), std::move(process_data)},
                          priority);
    } else {
      throw std::logic_error(
          "Cannot add a task after the worker queue has been shut down");
    }
  }

  /**
   * Notifies the thread pool that all tasks have been added.
   *
   * @throws logic_error If called twice.
   */
  void tasks_done() {
    if (!m_all_tasks_pushed) {
      m_all_tasks_pushed = true;
      m_worker_tasks.shutdown(m_threads);
    } else {
      throw std::logic_error("Worker queue is already shut down");
    }
  }

  /**
   * Terminates execution of all threads, if a call to process() has been made,
   * it will exit as well.
   *
   * Terminate is not instantaneous, all the calls which are being executed need
   * to finish first.
   */
  void terminate() { emergency_shutdown(); }

  /**
   * Waits for all the tasks to finish.
   *
   * Internally, it executes all the process_data calls using the current
   * thread.
   *
   * @throws any Exception which was reported by the produce_data or
   *         process_data calls.
   */
  void process() {
    try {
      while (true) {
        auto task = m_main_thread_tasks.pop();

        if (interrupted() || !task) {
          break;
        }

        task();

        if (interrupted()) {
          break;
        }
      }

      if (!interrupted()) {
        m_async_state = Async_state::DONE;
      }
    } catch (...) {
      kill_threads();
      throw;
    }

    wait_for_worker_threads();
    rethrow();
  }

  /**
   * Calls process() in a separate thread, the current state of the processing
   * can be checked at any time using the returned value.
   *
   * @returns value which can be used to check the current state of asynchronous
   *          processing
   */
  const std::atomic<Async_state> &process_async() {
    m_async_state = Async_state::PRODUCING;

    m_async_thread =
        std::make_unique<std::thread>(mysqlsh::spawn_scoped_thread([this]() {
          try {
            process();
          } catch (...) {
            m_async_exception = std::current_exception();
          }
        }));

    return m_async_state;
  }

  /**
   * Waits for the asynchronous process() call to finish.
   *
   * @throws any Exception which was reported by process() call.
   */
  void wait_for_process() {
    wait_for_async_thread();

    if (m_async_exception) {
      std::rethrow_exception(m_async_exception);
    }
  }

 private:
  struct Task {
    Producer produce_data;
    Processor process_data;
  };

  void emergency_shutdown() {
    // we check only local flag here, as we want to modify our internal state
    // regardless of the external state
    if (!m_worker_interrupt) {
      m_worker_interrupt = true;
      if (m_interrupted) m_interrupted->test_and_set();
      m_async_state = Async_state::TERMINATED;
      m_worker_tasks.shutdown(m_threads);
      shutdown_main_thread();
    }
  }

  void wait_for_worker_threads() {
    for (auto &worker : m_workers) {
      worker.join();
    }

    m_workers.clear();
  }

  void wait_for_async_thread() {
    if (m_async_thread) {
      m_async_thread->join();
      m_async_thread.reset();
    }
  }

  void rethrow() {
    for (const auto &exc : m_worker_exceptions) {
      if (exc) {
        std::rethrow_exception(exc);
      }
    }
  }

  void kill_threads() {
    if (!m_workers.empty()) {
      emergency_shutdown();
      wait_for_worker_threads();
    }
  }

  void maybe_shutdown_main_thread() {
    if (--m_active_threads == 0) {
      m_async_state = Async_state::PROCESSING;
      shutdown_main_thread();
    }
  }

  void shutdown_main_thread() { m_main_thread_tasks.shutdown(1); }

  inline bool interrupted() const noexcept {
    return m_worker_interrupt || (m_interrupted && m_interrupted->test());
  }

  uint64_t m_threads;

  std::atomic<uint64_t> m_active_threads;

  std::vector<std::thread> m_workers;

  std::vector<std::exception_ptr> m_worker_exceptions;

  std::atomic_bool m_worker_interrupt{false};

  shcore::atomic_flag *m_interrupted;

  std::atomic_bool m_all_tasks_pushed{false};

  Synchronized_queue<Task> m_worker_tasks;

  Synchronized_queue<std::function<void()>> m_main_thread_tasks;

  std::atomic<Async_state> m_async_state{Async_state::IDLE};

  std::unique_ptr<std::thread> m_async_thread;

  std::exception_ptr m_async_exception = nullptr;

  std::function<void()> m_thread_init;
  std::function<void()> m_thread_finish;
};

using Thread_pool = Thread_pool_base<std::string>;

}  // namespace shcore

#endif  // MYSQLSHDK_LIBS_UTILS_THREAD_POOL_H_
