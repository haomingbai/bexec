/**
 * @file include/bexec/stop_token.hpp
 * @brief Lightweight stop-token, stop-source, and callback types.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines never_stop_token plus inplace_stop_source, inplace_stop_token, and
 * inplace_stop_callback for cooperative cancellation in schedulers and
 * algorithms. Callback registrations are stored intrusively in the callback
 * object; registering a callback does not allocate.
 *
 * The callback list is guarded by a spinlock fused into the same atomic word
 * as the stop flag (bit 0: stop requested, bit 1: lock held), so the hot read
 * stop_requested() stays a single lock-free acquire load and no mutex or
 * futex exists anywhere on the stop-token path. Callbacks execute with the
 * lock open. See docs/stop-token.md for the full protocol specification.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_STOP_TOKEN_HPP_
#define BEXEC_INCLUDE_BEXEC_STOP_TOKEN_HPP_

#include <atomic>
#include <bexec/detail/type_traits.hpp>
#include <concepts>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <utility>

namespace bexec {

/**
 * @brief A stop token that can never request cancellation.
 */
class never_stop_token {
 public:
  template <class Callback>
  class callback_type {
   public:
    callback_type(const never_stop_token&, Callback) noexcept {}
  };

  /** @brief Always returns false. */
  [[nodiscard]] bool stop_requested() const noexcept { return false; }
};

namespace detail {

struct stop_state;

struct stop_callback_record {
  // List linkage: plain fields, guarded by the stop-state spinlock.
  // prev_ptr is the address of the slot pointing at this record; nullptr
  // marks a record that is no longer in the list.
  stop_callback_record* next{nullptr};
  stop_callback_record** prev_ptr{nullptr};

  // Execution handshake.
  bool* removed_during_callback{nullptr};  // flag owned by the notifying thread
  std::atomic<bool> completed{false};      // cross-thread completion signal

  // Dispatch.
  void (*invoke)(stop_callback_record*) noexcept {nullptr};
};

inline void cpu_pause() noexcept {
#if defined(__i386__) || defined(__x86_64__)
  asm volatile("pause");
#elif defined(__aarch64__) || defined(__arm__)
  asm volatile("yield");
#elif defined(__powerpc64__)
  asm volatile("or 27,27,27");
#endif
}

struct spin_wait {
  static constexpr std::uint32_t yield_threshold = 20;
  std::uint32_t count{0};

  void wait() noexcept {
    if (count++ < yield_threshold) {
      cpu_pause();
    } else {
      std::this_thread::yield();
    }
  }
};

struct stop_state {
  static constexpr std::uint8_t stop_requested_bit = 1;  // bit 0
  static constexpr std::uint8_t locked_bit = 2;          // bit 1

  // Stop flag and spinlock fused into one word.
  mutable std::atomic<std::uint8_t> state{0};
  // Plain pointer, guarded by the spinlock.
  stop_callback_record* head{nullptr};
  // Written under the lock by request_stop().
  std::thread::id requester_thread{};

  // Returns the pre-lock state value; the caller MUST pass it to unlock().
  std::uint8_t lock() noexcept {
    spin_wait spinner;
    std::uint8_t prev = state.load(std::memory_order_relaxed);
    for (;;) {
      while ((prev & locked_bit) != 0) {  // held: spin without CAS
        spinner.wait();
        prev = state.load(std::memory_order_relaxed);
      }
      // CAS from {0, 1} to {2, 3}. Acquire pairs with the previous unlock.
      if (state.compare_exchange_weak(prev, prev | locked_bit,
                                      std::memory_order_acquire,
                                      std::memory_order_relaxed)) {
        return prev;
      }
    }
  }

  // Restores the pre-lock value: keeps the requested bit iff it was set.
  void unlock(std::uint8_t prev) noexcept {
    state.store(prev, std::memory_order_release);
  }

  // Returns true iff the lock was acquired. With set_requested == true the
  // stop request is claimed in the same RMW. On success with
  // set_requested == false the caller must unlock(0).
  bool try_lock_unless_stop_requested(bool set_requested) noexcept {
    spin_wait spinner;
    std::uint8_t prev = state.load(std::memory_order_relaxed);
    for (;;) {
      if ((prev & stop_requested_bit) != 0) {
        return false;  // stop already requested: fail fast, no CAS
      }
      if (prev != 0) {  // == locked_bit: held, spin
        spinner.wait();
        prev = state.load(std::memory_order_relaxed);
        continue;
      }
      // Only ever CASes from 0. The claim fuses lock + request into one RMW.
      auto next = static_cast<std::uint8_t>(
          set_requested ? (locked_bit | stop_requested_bit) : locked_bit);
      if (state.compare_exchange_weak(prev, next, std::memory_order_acq_rel,
                                      std::memory_order_relaxed)) {
        return true;
      }
    }
  }

  // Registers a record by pushing it onto the list under the spinlock.
  // Returns false (without touching the list) if stop was already requested;
  // the caller then fires the callback inline.
  bool try_add_callback(stop_callback_record* record) noexcept {
    if (!try_lock_unless_stop_requested(false)) {
      return false;
    }
    // Plain writes under the spinlock. Push onto the head.
    record->next = head;
    record->prev_ptr = &head;
    if (record->next != nullptr) {
      record->next->prev_ptr = &record->next;
    }
    head = record;
    unlock(0);  // release-publishes the push
    return true;
  }

  // Unregistration, called by ~inplace_stop_callback for a registered record.
  void remove_callback(stop_callback_record* record) noexcept {
    std::uint8_t prev = lock();  // CAS acquire
    if (record->prev_ptr != nullptr) {
      // Still linked: uniform unlink, no head special case.
      *record->prev_ptr = record->next;
      if (record->next != nullptr) {
        record->next->prev_ptr = record->prev_ptr;
      }
      unlock(prev);
      return;
    }
    // Popped (or drained by ~inplace_stop_source): out of the list.
    std::thread::id notifier = requester_thread;  // plain read under lock
    unlock(prev);

    if (std::this_thread::get_id() == notifier) {
      // Reentrant destroy on the requesting thread, or the requesting thread
      // destroying a record that already completed: the nullptr guard skips
      // finished request_stop calls.
      if (record->removed_during_callback != nullptr) {
        *record->removed_during_callback = true;
      }
    } else {
      // Executing (or about to execute) on the requesting thread: wait.
      spin_wait spinner;
      while (!record->completed.load(std::memory_order_acquire)) {
        spinner.wait();
      }
    }
  }
};

}  // namespace detail

class inplace_stop_source;
class inplace_stop_token;

/**
 * @brief Registration object for callbacks attached to inplace_stop_token.
 *
 * Destroying the registration prevents future invocation if cancellation has
 * not already reached the callback. Callback invocation is one-shot.
 * The associated inplace_stop_source must outlive this registration.
 */
template <class Callback>
class inplace_stop_callback {
 public:
  inplace_stop_callback(const inplace_stop_token& token, Callback callback);

  inplace_stop_callback(const inplace_stop_callback&) = delete;
  inplace_stop_callback& operator=(const inplace_stop_callback&) = delete;

  inplace_stop_callback(inplace_stop_callback&&) = delete;
  inplace_stop_callback& operator=(inplace_stop_callback&&) = delete;

  ~inplace_stop_callback() {
    if (state_ != nullptr) {
      state_->remove_callback(&record_);
    }
  }

 private:
  struct owned_record : detail::stop_callback_record {
    void* owner{nullptr};
  };

  void invoke() noexcept { callback_(); }

  static void invoke_record(detail::stop_callback_record* record) noexcept {
    auto* owned = static_cast<owned_record*>(record);
    auto* self = static_cast<inplace_stop_callback*>(owned->owner);
    self->invoke();
  }

  std::decay_t<Callback> callback_;
  owned_record record_;
  detail::stop_state* state_{nullptr};
};

/**
 * @brief A lightweight copyable stop token associated with inplace_stop_source.
 */
class inplace_stop_token {
 public:
  template <class Callback>
  using callback_type = inplace_stop_callback<std::decay_t<Callback>>;

  inplace_stop_token() = default;

  /** @brief Returns true once the associated source has requested stop. */
  [[nodiscard]] bool stop_requested() const noexcept {
    return state_ != nullptr && (state_->state.load(std::memory_order_acquire) &
                                 detail::stop_state::stop_requested_bit) != 0;
  }

 private:
  friend class inplace_stop_source;
  template <class Callback>
  friend class inplace_stop_callback;

  explicit inplace_stop_token(detail::stop_state& state) : state_(&state) {}

  detail::stop_state* state_{nullptr};
};

/**
 * @brief A small in-place cancellation source.
 *
 * request_stop() is thread-safe. Registered callbacks either run when stop is
 * requested or, if stop was already requested, during registration.
 * Associated tokens and callbacks must not be used after this source is
 * destroyed.
 */
class inplace_stop_source {
 public:
  inplace_stop_source() = default;
  inplace_stop_source(const inplace_stop_source&) = delete;
  inplace_stop_source& operator=(const inplace_stop_source&) = delete;

  // Defensive drain: unlinks registrations that were never destroyed before
  // the source died (excluded by the lifetime contract, kept as a safety
  // net) and pre-marks each drained record completed so a later unregister
  // cannot wait on a completion that will never happen.
  ~inplace_stop_source() {
    std::uint8_t prev = state_.lock();
    while (state_.head != nullptr) {
      detail::stop_callback_record* record = state_.head;
      state_.head = record->next;
      if (state_.head != nullptr) {
        state_.head->prev_ptr = &state_.head;
      }
      record->prev_ptr = nullptr;  // mark removed
      record->next = nullptr;
      record->completed.store(true, std::memory_order_release);
    }
    state_.unlock(prev);
  }

  /** @brief Returns a token connected to this source. */
  [[nodiscard]] inplace_stop_token get_token() noexcept {
    return inplace_stop_token{state_};
  }

  [[nodiscard]] inplace_stop_token get_token() const noexcept {
    return inplace_stop_token{const_cast<detail::stop_state&>(state_)};
  }

  /** @brief Returns whether stop has already been requested. */
  [[nodiscard]] bool stop_requested() const noexcept {
    return (state_.state.load(std::memory_order_acquire) &
            detail::stop_state::stop_requested_bit) != 0;
  }

  /**
   * @brief Requests stop and invokes registered callbacks once.
   * @return true if this call made the stop request, false if it was already
   * requested.
   */
  bool request_stop() noexcept {
    // The claim fuses lock acquisition and the stop request into one RMW;
    // the loser of the claim returns false without touching the list.
    if (!state_.try_lock_unless_stop_requested(true)) {
      return false;
    }
    // This call holds the lock and the requested bit is set (state == 3).
    state_.requester_thread = std::this_thread::get_id();

    while (state_.head != nullptr) {
      detail::stop_callback_record* record = state_.head;
      record->prev_ptr = nullptr;  // "popped" marker, under lock
      state_.head = record->next;
      if (state_.head != nullptr) {
        state_.head->prev_ptr = &state_.head;
      }
      bool removed_during_callback = false;
      record->removed_during_callback = &removed_during_callback;

      // ONE store: opens the lock AND keeps the requested flag visible.
      state_.state.store(detail::stop_state::stop_requested_bit,
                         std::memory_order_release);

      record->invoke(record);  // callback runs with the lock OPEN

      if (!removed_during_callback) {
        // The record is still valid: finalize it so unregisterers stop
        // waiting. If it was destroyed inside its own callback body, it is
        // dangling and must not be touched.
        record->removed_during_callback = nullptr;
        record->completed.store(true, std::memory_order_release);
      }

      state_.lock();  // re-lock: CAS acquire between callbacks
    }

    // Final unlock: the requested bit is sticky.
    state_.state.store(detail::stop_state::stop_requested_bit,
                       std::memory_order_release);
    return true;
  }

 private:
  detail::stop_state state_;
};

template <class Callback>
inplace_stop_callback<Callback>::inplace_stop_callback(
    const inplace_stop_token& token, Callback callback)
    : callback_(std::move(callback)) {
  record_.owner = this;
  record_.invoke = &inplace_stop_callback::invoke_record;

  detail::stop_state* state = token.state_;
  if (state == nullptr) {
    return;  // null token: never fires
  }

  if (state->try_add_callback(&record_)) {
    state_ = state;  // registered
    return;
  }

  // Stop already requested: fire inline, never enters the list. The
  // destructor becomes a no-op (state_ stays nullptr).
  record_.invoke(&record_);
}

/**
 * @brief Concept for stop-token-like types used by bexec.
 */
template <class Token>
concept stop_token =
    std::copy_constructible<Token> && requires(const Token& token) {
      { token.stop_requested() } -> std::same_as<bool>;
      typename Token::template callback_type<detail::empty_callback>;
    };

/**
 * @brief Concept for stop-source-like types used by bexec.
 */
template <class Source>
concept stop_source = requires(Source& source) {
  { source.request_stop() } -> std::same_as<bool>;
  { source.stop_requested() } -> std::same_as<bool>;
  { source.get_token() } -> stop_token;
};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_STOP_TOKEN_HPP_
