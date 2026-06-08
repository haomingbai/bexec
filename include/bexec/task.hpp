/**
 * @file include/bexec/task.hpp
 * @brief Small lazy coroutine task type.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines task<T> and task<void> for examples and tests, including stored
 * result and exception handling.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_TASK_HPP_
#define BEXEC_INCLUDE_BEXEC_TASK_HPP_

#include <bexec/detail/config.hpp>
#include <cassert>
#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace bexec {

/**
 * @brief Coroutine task type used by bexec examples and tests.
 *
 * The task is lazy: call start() to run until the first suspension. result()
 * consumes the stored result after done() is true.
 */
template <class T>
class task {
 public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type {
    std::optional<T> value;
    std::exception_ptr error;

    task get_return_object() { return task{handle_type::from_promise(*this)}; }

    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }

    template <class U>
    void return_value(U&& result) {
      value.emplace(std::forward<U>(result));
    }

    void unhandled_exception() noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      error = std::current_exception();
#else
      assert(false);
      BEXEC_DETAIL_UNREACHABLE();
#endif
    }
  };

  task() = default;
  explicit task(handle_type handle) : handle_(handle) {}

  task(const task&) = delete;
  task& operator=(const task&) = delete;

  task(task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

  task& operator=(task&& other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~task() {
    if (handle_) {
      handle_.destroy();
    }
  }

  /** @brief Starts or resumes the coroutine. */
  void start() {
    if (handle_ && !handle_.done()) {
      handle_.resume();
    }
  }

  /** @brief Returns true when the coroutine reached final suspend. */
  [[nodiscard]] bool done() const noexcept {
    return !handle_ || handle_.done();
  }

  /** @brief Returns the result, rethrowing any stored exception. */
  T result() {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    if (handle_.promise().error) {
      std::rethrow_exception(handle_.promise().error);
    }
#endif
    return std::move(*handle_.promise().value);
  }

 private:
  handle_type handle_{};
};

/**
 * @brief Void specialization of task.
 */
template <>
class task<void> {
 public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type {
    std::exception_ptr error;

    task get_return_object() { return task{handle_type::from_promise(*this)}; }

    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}

    void unhandled_exception() noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      error = std::current_exception();
#else
      assert(false);
      BEXEC_DETAIL_UNREACHABLE();
#endif
    }
  };

  task() = default;
  explicit task(handle_type handle) : handle_(handle) {}

  task(const task&) = delete;
  task& operator=(const task&) = delete;

  task(task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

  task& operator=(task&& other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~task() {
    if (handle_) {
      handle_.destroy();
    }
  }

  /** @brief Starts or resumes the coroutine. */
  void start() {
    if (handle_ && !handle_.done()) {
      handle_.resume();
    }
  }

  /** @brief Returns true when the coroutine reached final suspend. */
  [[nodiscard]] bool done() const noexcept {
    return !handle_ || handle_.done();
  }

  /** @brief Rethrows any stored exception. */
  void result() {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    if (handle_.promise().error) {
      std::rethrow_exception(handle_.promise().error);
    }
#endif
  }

 private:
  handle_type handle_{};
};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_TASK_HPP_
