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
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_STOP_TOKEN_HPP_
#define BEXEC_INCLUDE_BEXEC_STOP_TOKEN_HPP_

#include <bexec/detail/type_traits.hpp>
#include <concepts>
#include <exception>
#include <mutex>
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
  stop_callback_record* next{nullptr};
  stop_callback_record* prev{nullptr};
  stop_state* state{nullptr};
  void (*invoke)(stop_callback_record*) noexcept {nullptr};
};

struct stop_state {
  mutable std::mutex mutex;
  bool requested{false};
  stop_callback_record* head{nullptr};

  void push(stop_callback_record& record) noexcept {
    record.state = this;
    record.prev = nullptr;
    record.next = head;
    if (head != nullptr) {
      head->prev = &record;
    }
    head = &record;
  }

  void unlink(stop_callback_record& record) noexcept {
    if (record.prev != nullptr) {
      record.prev->next = record.next;
    } else if (head == &record) {
      head = record.next;
    }

    if (record.next != nullptr) {
      record.next->prev = record.prev;
    }

    record.next = nullptr;
    record.prev = nullptr;
    record.state = nullptr;
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
 */
template <class Callback>
class inplace_stop_callback {
 public:
  inplace_stop_callback(const inplace_stop_token& token, Callback callback);

  inplace_stop_callback(const inplace_stop_callback&) = delete;
  inplace_stop_callback& operator=(const inplace_stop_callback&) = delete;

  inplace_stop_callback(inplace_stop_callback&&) = delete;
  inplace_stop_callback& operator=(inplace_stop_callback&&) = delete;

  ~inplace_stop_callback() { unregister(); }

 private:
  void unregister() noexcept {
    auto* state = record_.state;
    if (state == nullptr) {
      return;
    }

    std::lock_guard lock(state->mutex);
    if (record_.state != nullptr) {
      record_.state->unlink(record_);
    }
  }

  std::decay_t<Callback> callback_;
  struct owned_record : detail::stop_callback_record {
    void* owner{nullptr};
  } record_;
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
    if (state_ == nullptr) {
      return false;
    }

    std::lock_guard lock(state_->mutex);
    return state_->requested;
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
 */
class inplace_stop_source {
 public:
  inplace_stop_source() = default;
  inplace_stop_source(const inplace_stop_source&) = delete;
  inplace_stop_source& operator=(const inplace_stop_source&) = delete;

  ~inplace_stop_source() {
    std::lock_guard lock(state_.mutex);
    while (state_.head != nullptr) {
      state_.unlink(*state_.head);
    }
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
    std::lock_guard lock(state_.mutex);
    return state_.requested;
  }

  /**
   * @brief Requests stop and invokes registered callbacks once.
   * @return true if this call made the stop request, false if it was already
   * requested.
   */
  bool request_stop() noexcept {
    std::unique_lock lock(state_.mutex);
    if (state_.requested) {
      return false;
    }
    state_.requested = true;

    while (state_.head != nullptr) {
      detail::stop_callback_record* record = state_.head;
      state_.unlink(*record);
      auto* invoke = record->invoke;
      lock.unlock();
      invoke(record);
      lock.lock();
    }
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
  record_.invoke = [](detail::stop_callback_record* record) noexcept {
    auto* owned = static_cast<owned_record*>(record);
    auto* self = static_cast<inplace_stop_callback*>(owned->owner);
    try {
      self->callback_();
    } catch (...) {
      std::terminate();
    }
  };

  detail::stop_state* state = token.state_;
  if (state == nullptr) {
    return;
  }

  bool fire_now = false;
  {
    std::lock_guard lock(state->mutex);
    if (state->requested) {
      fire_now = true;
    } else {
      state->push(record_);
    }
  }

  if (fire_now) {
    record_.invoke(&record_);
  }
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
