/**
 * @file include/bexec/run_loop.hpp
 * @brief Stack-owned intrusive FIFO run loop and scheduler.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_RUN_LOOP_HPP_
#define BEXEC_INCLUDE_BEXEC_RUN_LOOP_HPP_

#include <atomic>
#include <bexec/completion_signatures.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/scheduler.hpp>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace bexec {

namespace detail {
struct run_loop_operation_base {
  run_loop_operation_base* next{nullptr};
  virtual ~run_loop_operation_base() = default;
  virtual void execute() noexcept = 0;
};

struct run_loop_closed_operation final : run_loop_operation_base {
  void execute() noexcept override { assert(false); }
};

inline run_loop_operation_base* run_loop_closed_stack() noexcept {
  static run_loop_closed_operation operation;
  return &operation;
}

class run_loop_schedule_sender;
}  // namespace detail

/**
 * @brief A minimal thread-safe FIFO run loop for scheduling operation states.
 */
class run_loop {
 private:
  enum class run_loop_state { starting, running, finishing, finished };

 public:
  class scheduler;

  run_loop() noexcept = default;
  run_loop(const run_loop&) = delete;
  run_loop& operator=(const run_loop&) = delete;
  ~run_loop() noexcept {
    detail::run_loop_operation_base* head =
        head_.load(std::memory_order_acquire);
    assert(head == nullptr || head == detail::run_loop_closed_stack());
    assert(state_.load(std::memory_order_acquire) != run_loop_state::running);
    (void)head;
  }

  [[nodiscard]] scheduler get_scheduler() noexcept;

  /** @brief Runs queued work until finish() is called and the queue is empty.
   */
  void run() noexcept {
    if (!begin_run()) {
      return;
    }
    for (;;) {
      detail::run_loop_operation_base* operations = pop_all_blocking();
      if (operations == nullptr) {
        return;
      }
      execute_all(operations);
    }
  }

  /** @brief Requests that run() return after already queued work is drained. */
  void finish() noexcept {
    run_loop_state expected = run_loop_state::starting;
    if (!state_.compare_exchange_strong(expected, run_loop_state::finishing,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
      if (expected != run_loop_state::running ||
          !state_.compare_exchange_strong(expected, run_loop_state::finishing,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
        assert(false);
        return;
      }
    }
    wake_all_waiters();
  }

 private:
  friend class detail::run_loop_schedule_sender;

  void enqueue(detail::run_loop_operation_base& operation) noexcept {
    detail::run_loop_operation_base* head =
        head_.load(std::memory_order_acquire);
    do {
      if (head == detail::run_loop_closed_stack()) {
        assert(false);
        return;
      }
      operation.next = head;
    } while (!head_.compare_exchange_weak(head, &operation,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire));

    wake_one_waiter();
  }

  void wake_one_waiter() noexcept {
    // Queue/state publication is atomic. The mutex only serializes with
    // wait() entering sleep after a false predicate check.
    std::lock_guard lock(mutex_);
    cv_.notify_one();
  }

  void wake_all_waiters() noexcept {
    // Queue/state publication is atomic. The mutex only serializes with
    // wait() entering sleep after a false predicate check.
    std::lock_guard lock(mutex_);
    cv_.notify_all();
  }

  detail::run_loop_operation_base* pop_all_blocking() noexcept {
    for (;;) {
      if (detail::run_loop_operation_base* operations = try_pop_all()) {
        return reverse(operations);
      }
      if (state_.load(std::memory_order_acquire) == run_loop_state::finishing &&
          try_finish_empty()) {
        return nullptr;
      }

      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] {
        return has_work() || state_.load(std::memory_order_acquire) ==
                                 run_loop_state::finishing;
      });
    }
  }

  detail::run_loop_operation_base* try_pop_all() noexcept {
    detail::run_loop_operation_base* operations =
        head_.load(std::memory_order_acquire);
    while (operations != nullptr &&
           operations != detail::run_loop_closed_stack()) {
      if (head_.compare_exchange_weak(operations, nullptr,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire)) {
        return operations;
      }
    }
    return nullptr;
  }

  bool try_finish_empty() noexcept {
    detail::run_loop_operation_base* expected = nullptr;
    if (!head_.compare_exchange_strong(
            expected, detail::run_loop_closed_stack(),
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      return false;
    }

    run_loop_state state = run_loop_state::finishing;
    const bool exchanged = state_.compare_exchange_strong(
        state, run_loop_state::finished, std::memory_order_acq_rel,
        std::memory_order_acquire);
    assert(exchanged);
    (void)exchanged;
    return true;
  }

  [[nodiscard]] bool has_work() const noexcept {
    detail::run_loop_operation_base* head =
        head_.load(std::memory_order_acquire);
    return head != nullptr && head != detail::run_loop_closed_stack();
  }

  static detail::run_loop_operation_base* reverse(
      detail::run_loop_operation_base* operations) noexcept {
    detail::run_loop_operation_base* reversed = nullptr;
    while (operations != nullptr) {
      detail::run_loop_operation_base* next = operations->next;
      operations->next = reversed;
      reversed = operations;
      operations = next;
    }
    return reversed;
  }

  static void execute_all(
      detail::run_loop_operation_base* operations) noexcept {
    while (operations != nullptr) {
      detail::run_loop_operation_base* operation = operations;
      operations = operations->next;
      operation->next = nullptr;
      operation->execute();
    }
  }

  bool begin_run() noexcept {
    run_loop_state expected = run_loop_state::starting;
    if (state_.compare_exchange_strong(expected, run_loop_state::running,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
      return true;
    }
    if (expected == run_loop_state::finishing) {
      return true;
    }
    assert(false);
    return false;
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<detail::run_loop_operation_base*> head_{nullptr};
  std::atomic<run_loop_state> state_{run_loop_state::starting};
};

/**
 * @brief Scheduler handle for run_loop.
 */
class run_loop::scheduler {
 public:
  scheduler() = default;

  [[nodiscard]] detail::run_loop_schedule_sender schedule() const;

  friend bool operator==(scheduler lhs, scheduler rhs) noexcept {
    return lhs.loop_ == rhs.loop_;
  }

 private:
  friend class run_loop;

  explicit scheduler(run_loop& loop) : loop_(&loop) {}

  run_loop* loop_{nullptr};
};

inline run_loop::scheduler run_loop::get_scheduler() noexcept {
  return scheduler{*this};
}

namespace detail {

class run_loop_schedule_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<set_value_t(), set_stopped_t()>;

  explicit run_loop_schedule_sender(run_loop& loop) : loop_(&loop) {}

  template <class Receiver>
  class operation : public run_loop_operation_base {
   public:
    operation(run_loop& loop, Receiver receiver)
        : loop_(&loop), receiver_(std::move(receiver)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    void start() noexcept { loop_->enqueue(*this); }

    void execute() noexcept override {
      auto token =
          bexec::query(bexec::get_env(receiver_), bexec::get_stop_token);
      if (token.stop_requested()) {
        bexec::set_stopped(std::move(receiver_));
      } else {
        bexec::set_value(std::move(receiver_));
      }
    }

   private:
    run_loop* loop_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{*loop_, std::move(receiver)};
  }

 private:
  run_loop* loop_;
};

}  // namespace detail

inline detail::run_loop_schedule_sender run_loop::scheduler::schedule() const {
  return detail::run_loop_schedule_sender{*loop_};
}

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_RUN_LOOP_HPP_
