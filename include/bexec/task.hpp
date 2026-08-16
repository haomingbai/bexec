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
#include <bexec/completion_signatures.hpp>
#include <bexec/detail/config.hpp>
#include <bexec/receiver.hpp>
#include <cassert>
#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
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

  // The coroutine reached its end: deliver the stored completion to the
  // connected receiver, if any. The receiver may resume (and thereby destroy)
  // awaiting coroutines from here, so nothing touches the frame afterwards.
  void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
    handle.promise().dispatch_connected_completion();
  }

  void await_resume() const noexcept {}
};

/**
 * @brief Operation state produced by connecting a task to a receiver.
 *
 * Owns the coroutine frame moved out of the task and destroys it on
 * destruction. Immovable because its address is registered in the promise for
 * completion delivery.
 */
template <class Promise, class Receiver>
class task_operation {
 public:
  using handle_type = std::coroutine_handle<Promise>;

  // The frame is taken out of `task_handle` only after the receiver is
  // stored, so a throwing receiver move leaves the source task intact.
  task_operation(handle_type& task_handle, Receiver&& receiver) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : receiver_(std::move(receiver)),
        handle_(std::exchange(task_handle, {})) {
    handle_.promise().register_connected_operation(
        this, &task_operation::deliver);
  }

  task_operation(const task_operation&) = delete;
  task_operation& operator=(const task_operation&) = delete;
  task_operation(task_operation&&) = delete;
  task_operation& operator=(task_operation&&) = delete;

  ~task_operation() {
    if (handle_) {
      handle_.destroy();
    }
  }

  void start() noexcept { handle_.resume(); }

 private:
  static void deliver(void* operation_address) noexcept {
    auto& self = *static_cast<task_operation*>(operation_address);
    self.handle_.promise().deliver_to(std::move(self.receiver_));
  }

  Receiver receiver_;
  handle_type handle_;
};

template <class Promise>
void store_task_exception(Promise& promise) noexcept {
  promise.error_ = std::current_exception();
}

}  // namespace detail

/**
 * @brief Lazy, move-only coroutine task.
 *
 * A task is a single-shot sender: it connects to exactly one receiver through
 * an rvalue-only connect(), and the returned operation owns the coroutine
 * frame. A task that is never connected can still be driven manually with
 * start()/result().
 */
template <class T>
class task {
 public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  /**
   * @brief Exact completion contract of the task sender.
   *
   * The coroutine result is delivered as set_value(T), an escaping exception
   * as set_error(std::exception_ptr), and a stopped signal from an awaited
   * sender as set_stopped().
   */
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(T),
                                   bexec::set_error_t(std::exception_ptr),
                                   bexec::set_stopped_t()>;

  struct promise_type : with_awaitable_senders<promise_type> {
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

    /**
     * @brief Marks the task stopped and reports to the connected receiver.
     *
     * Called by the sender bridge when an awaited sender completes stopped.
     * The coroutine stays suspended at the await point and is destroyed with
     * its owner; the stopped completion reaches the awaiting chain through
     * the connected receiver.
     */
    [[nodiscard]] std::coroutine_handle<> unhandled_stopped() noexcept {
      stopped_ = true;
      dispatch_connected_completion();
      return std::noop_coroutine();
    }

    T consume_result() {
      rethrow_error();
      if (stopped_) {
        throw task_stopped{};
      }
      assert(value_.has_value());
      return std::move(*value_);
    }

    /**
     * @brief Delivers the stored completion to a connected receiver.
     *
     * Invoked through the registered thunk once the task finishes. Receivers
     * with a throwing-move value type are rejected by the set_value contract,
     * so the calls themselves never throw.
     */
    template <class Receiver>
    void deliver_to(Receiver&& receiver) noexcept {
      if (error_) {
        bexec::set_error(std::forward<Receiver>(receiver), error_);
      } else if (stopped_) {
        bexec::set_stopped(std::forward<Receiver>(receiver));
      } else {
        assert(value_.has_value());
        bexec::set_value(std::forward<Receiver>(receiver), std::move(*value_));
      }
    }

    /**
     * @brief Registers the owning operation for completion delivery.
     *
     * connect() stores the operation address and a type-erased delivery thunk
     * so final_suspend/unhandled_stopped can complete the receiver without
     * the promise knowing its type.
     */
    void register_connected_operation(
        void* operation, void (*deliver)(void*) noexcept) noexcept {
      connected_operation_ = operation;
      deliver_connected_ = deliver;
    }

    void dispatch_connected_completion() noexcept {
      if (deliver_connected_ != nullptr) {
        deliver_connected_(connected_operation_);
      }
    }

   private:
    void rethrow_error() {
      if (error_) {
        std::rethrow_exception(error_);
      }
    }

    friend void detail::store_task_exception<promise_type>(
        promise_type&) noexcept;

    std::optional<T> value_;
    std::exception_ptr error_;
    bool stopped_{false};
    void* connected_operation_{nullptr};
    void (*deliver_connected_)(void*) noexcept {nullptr};
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
   * A task waiting for a sender is resumed by that operation and must not be
   * manually resumed.
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

  /**
   * @brief Connects this task to a receiver, transferring frame ownership.
   *
   * Rvalue-only: a task wraps a single-shot coroutine frame that can be
   * resumed by exactly one operation, so it can be connected at most once and
   * must not have been started already. The returned operation owns the frame
   * and destroys it on destruction.
   */
  template <class Receiver>
    requires bexec::receiver_of<std::remove_cvref_t<Receiver>,
                                completion_signatures>
  [[nodiscard]] detail::task_operation<promise_type,
                                       std::remove_cvref_t<Receiver>>
  connect(Receiver&& receiver) && noexcept(
      std::is_nothrow_move_constructible_v<std::remove_cvref_t<Receiver>>) {
    assert(handle_ && "connect requires a task that still owns its frame");
    return detail::task_operation<promise_type,
                                  std::remove_cvref_t<Receiver>>{
        handle_, std::forward<Receiver>(receiver)};
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

  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(),
                                   bexec::set_error_t(std::exception_ptr),
                                   bexec::set_stopped_t()>;

  struct promise_type : with_awaitable_senders<promise_type> {
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
      dispatch_connected_completion();
      return std::noop_coroutine();
    }

    void consume_result() {
      rethrow_error();
      if (stopped_) {
        throw task_stopped{};
      }
    }

    template <class Receiver>
    void deliver_to(Receiver&& receiver) noexcept {
      if (error_) {
        bexec::set_error(std::forward<Receiver>(receiver), error_);
      } else if (stopped_) {
        bexec::set_stopped(std::forward<Receiver>(receiver));
      } else {
        bexec::set_value(std::forward<Receiver>(receiver));
      }
    }

    void register_connected_operation(
        void* operation, void (*deliver)(void*) noexcept) noexcept {
      connected_operation_ = operation;
      deliver_connected_ = deliver;
    }

    void dispatch_connected_completion() noexcept {
      if (deliver_connected_ != nullptr) {
        deliver_connected_(connected_operation_);
      }
    }

   private:
    void rethrow_error() {
      if (error_) {
        std::rethrow_exception(error_);
      }
    }

    friend void detail::store_task_exception<promise_type>(
        promise_type&) noexcept;

    std::exception_ptr error_;
    bool stopped_{false};
    void* connected_operation_{nullptr};
    void (*deliver_connected_)(void*) noexcept {nullptr};
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

  template <class Receiver>
    requires bexec::receiver_of<std::remove_cvref_t<Receiver>,
                                completion_signatures>
  [[nodiscard]] detail::task_operation<promise_type,
                                       std::remove_cvref_t<Receiver>>
  connect(Receiver&& receiver) && noexcept(
      std::is_nothrow_move_constructible_v<std::remove_cvref_t<Receiver>>) {
    assert(handle_ && "connect requires a task that still owns its frame");
    return detail::task_operation<promise_type,
                                  std::remove_cvref_t<Receiver>>{
        handle_, std::forward<Receiver>(receiver)};
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
