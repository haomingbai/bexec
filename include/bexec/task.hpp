/**
 * @file include/bexec/task.hpp
 * @brief Lazy coroutine task with sender awaiting support.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_TASK_HPP_
#define BEXEC_INCLUDE_BEXEC_TASK_HPP_

#include <bexec/awaitable.hpp>
#include <bexec/detail/config.hpp>
#include <cassert>
#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace bexec {

/**
 * @brief Thrown by task::result() when the task completed with stopped.
 */
class task_stopped : public std::exception {
 public:
  [[nodiscard]] const char* what() const noexcept override {
    return "bexec task stopped";
  }
};

namespace detail {

template <class Promise>
class task_final_awaiter {
 public:
  [[nodiscard]] bool await_ready() const noexcept { return false; }

  std::coroutine_handle<> await_suspend(
      std::coroutine_handle<Promise> handle) const noexcept {
    return handle.promise().continuation();
  }

  void await_resume() const noexcept {}
};

template <class Promise>
class task_awaiter {
 public:
  using handle_type = std::coroutine_handle<Promise>;

  explicit task_awaiter(handle_type handle) noexcept : handle_(handle) {}

  task_awaiter(const task_awaiter&) = delete;
  task_awaiter& operator=(const task_awaiter&) = delete;
  task_awaiter(task_awaiter&&) = delete;
  task_awaiter& operator=(task_awaiter&&) = delete;

  ~task_awaiter() {
    if (handle_) {
      handle_.destroy();
    }
  }

  [[nodiscard]] bool await_ready() const noexcept {
    return !handle_ || handle_.done();
  }

  template <class ParentPromise>
  std::coroutine_handle<> await_suspend(
      std::coroutine_handle<ParentPromise> parent) noexcept {
    if (handle_.promise().stopped()) {
      if constexpr (requires {
                      {
                        parent.promise().unhandled_stopped()
                      } -> std::convertible_to<std::coroutine_handle<>>;
                    }) {
        return static_cast<std::coroutine_handle<>>(
            parent.promise().unhandled_stopped());
      } else {
        assert(false);
        BEXEC_DETAIL_UNREACHABLE();
      }
    }

    handle_.promise().set_continuation(parent);
    return handle_;
  }

  decltype(auto) await_resume() {
    return handle_.promise().consume_await_result();
  }

 private:
  handle_type handle_;
};

template <class Promise>
void store_task_exception(Promise& promise) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
  promise.error_ = std::current_exception();
#else
  (void)promise;
  assert(false);
  BEXEC_DETAIL_UNREACHABLE();
#endif
}

}  // namespace detail

/**
 * @brief Lazy, move-only coroutine task.
 */
template <class T>
class task {
 public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type : with_awaitable_senders<promise_type> {
    using awaitable_base = with_awaitable_senders<promise_type>;

    task get_return_object() noexcept {
      return task{handle_type::from_promise(*this)};
    }

    std::suspend_always initial_suspend() const noexcept { return {}; }
    detail::task_final_awaiter<promise_type> final_suspend() const noexcept {
      return {};
    }

    template <class U>
    void return_value(U&& result) {
      value_.emplace(std::forward<U>(result));
    }

    void unhandled_exception() noexcept { detail::store_task_exception(*this); }

    [[nodiscard]] bool stopped() const noexcept { return stopped_; }

    [[nodiscard]] std::coroutine_handle<> unhandled_stopped() noexcept {
      stopped_ = true;
      if (this->continuation() == std::noop_coroutine()) {
        return std::noop_coroutine();
      }
      return awaitable_base::unhandled_stopped();
    }

    T consume_result() {
      rethrow_error();
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      if (stopped_) {
        throw task_stopped{};
      }
#else
      assert(!stopped_);
#endif
      assert(value_.has_value());
      return std::move(*value_);
    }

    T consume_await_result() {
      rethrow_error();
      assert(!stopped_);
      assert(value_.has_value());
      return std::move(*value_);
    }

   private:
    void rethrow_error() {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      if (error_) {
        std::rethrow_exception(error_);
      }
#else
      assert(!error_);
#endif
    }

    friend void detail::store_task_exception<promise_type>(
        promise_type&) noexcept;

    std::optional<T> value_;
    std::exception_ptr error_;
    bool stopped_{false};
  };

  task() noexcept = default;
  explicit task(handle_type handle) noexcept : handle_(handle) {}

  task(const task&) = delete;
  task& operator=(const task&) = delete;

  task(task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

  task& operator=(task&& other) noexcept {
    if (this != &other) {
      destroy();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~task() { destroy(); }

  /**
   * @brief Starts or manually resumes the task.
   *
   * A task waiting for a sender or child task is resumed by that operation and
   * must not be manually resumed.
   */
  void start() {
    if (handle_ && !done()) {
      handle_.resume();
    }
  }

  [[nodiscard]] bool done() const noexcept {
    return !handle_ || handle_.done() || handle_.promise().stopped();
  }

  T result() {
    assert(handle_);
    assert(done());
    return handle_.promise().consume_result();
  }

  detail::task_awaiter<promise_type> operator co_await() && noexcept {
    return detail::task_awaiter<promise_type>{
        std::exchange(handle_, handle_type{})};
  }

 private:
  void destroy() noexcept {
    if (handle_) {
      handle_.destroy();
      handle_ = {};
    }
  }

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

  struct promise_type : with_awaitable_senders<promise_type> {
    using awaitable_base = with_awaitable_senders<promise_type>;

    task get_return_object() noexcept {
      return task{handle_type::from_promise(*this)};
    }

    std::suspend_always initial_suspend() const noexcept { return {}; }
    detail::task_final_awaiter<promise_type> final_suspend() const noexcept {
      return {};
    }

    void return_void() const noexcept {}

    void unhandled_exception() noexcept { detail::store_task_exception(*this); }

    [[nodiscard]] bool stopped() const noexcept { return stopped_; }

    [[nodiscard]] std::coroutine_handle<> unhandled_stopped() noexcept {
      stopped_ = true;
      if (this->continuation() == std::noop_coroutine()) {
        return std::noop_coroutine();
      }
      return awaitable_base::unhandled_stopped();
    }

    void consume_result() {
      rethrow_error();
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      if (stopped_) {
        throw task_stopped{};
      }
#else
      assert(!stopped_);
#endif
    }

    void consume_await_result() {
      rethrow_error();
      assert(!stopped_);
    }

   private:
    void rethrow_error() {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      if (error_) {
        std::rethrow_exception(error_);
      }
#else
      assert(!error_);
#endif
    }

    friend void detail::store_task_exception<promise_type>(
        promise_type&) noexcept;

    std::exception_ptr error_;
    bool stopped_{false};
  };

  task() noexcept = default;
  explicit task(handle_type handle) noexcept : handle_(handle) {}

  task(const task&) = delete;
  task& operator=(const task&) = delete;

  task(task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

  task& operator=(task&& other) noexcept {
    if (this != &other) {
      destroy();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~task() { destroy(); }

  void start() {
    if (handle_ && !done()) {
      handle_.resume();
    }
  }

  [[nodiscard]] bool done() const noexcept {
    return !handle_ || handle_.done() || handle_.promise().stopped();
  }

  void result() {
    assert(handle_);
    assert(done());
    handle_.promise().consume_result();
  }

  detail::task_awaiter<promise_type> operator co_await() && noexcept {
    return detail::task_awaiter<promise_type>{
        std::exchange(handle_, handle_type{})};
  }

 private:
  void destroy() noexcept {
    if (handle_) {
      handle_.destroy();
      handle_ = {};
    }
  }

  handle_type handle_{};
};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_TASK_HPP_
