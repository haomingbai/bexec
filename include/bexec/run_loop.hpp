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

#include <bexec/completion_signatures.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/scheduler.hpp>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace bexec {

namespace detail {
class run_loop_schedule_sender;
}

/**
 * @brief A minimal thread-safe FIFO run loop for scheduling operation states.
 */
class run_loop {
 public:
  class scheduler;

  struct operation_base {
    operation_base* next{nullptr};
    virtual ~operation_base() = default;
    virtual void execute() noexcept = 0;
  };

  run_loop() = default;
  run_loop(const run_loop&) = delete;
  run_loop& operator=(const run_loop&) = delete;

  [[nodiscard]] scheduler get_scheduler() noexcept;

  /** @brief Runs queued work until finish() is called and the queue is empty.
   */
  void run() {
    for (;;) {
      operation_base* operation = pop_blocking();
      if (operation == nullptr) {
        return;
      }
      operation->execute();
    }
  }

  /** @brief Runs one queued item if available. */
  std::size_t run_one() {
    operation_base* operation = pop_nonblocking();
    if (operation == nullptr) {
      return 0;
    }

    operation->execute();
    return 1;
  }

  /** @brief Allows a later run() to block again after a previous finish(). */
  void reset() noexcept {
    std::lock_guard lock(mutex_);
    finishing_ = false;
  }

  /** @brief Requests that run() return after already queued work is drained. */
  void finish() noexcept {
    {
      std::lock_guard lock(mutex_);
      finishing_ = true;
    }
    cv_.notify_all();
  }

 private:
  friend class detail::run_loop_schedule_sender;

  void enqueue(operation_base& operation) noexcept {
    {
      std::lock_guard lock(mutex_);
      operation.next = nullptr;
      if (tail_ == nullptr) {
        head_ = &operation;
      } else {
        tail_->next = &operation;
      }
      tail_ = &operation;
    }
    cv_.notify_one();
  }

  operation_base* pop_nonblocking() noexcept {
    std::lock_guard lock(mutex_);
    return pop_locked();
  }

  operation_base* pop_blocking() noexcept {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return head_ != nullptr || finishing_; });
    if (head_ == nullptr) {
      return nullptr;
    }
    return pop_locked();
  }

  operation_base* pop_locked() noexcept {
    operation_base* operation = head_;
    if (operation == nullptr) {
      return nullptr;
    }

    head_ = operation->next;
    if (head_ == nullptr) {
      tail_ = nullptr;
    }
    operation->next = nullptr;
    return operation;
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  operation_base* head_{nullptr};
  operation_base* tail_{nullptr};
  bool finishing_{false};
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
  class operation : public run_loop::operation_base {
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
