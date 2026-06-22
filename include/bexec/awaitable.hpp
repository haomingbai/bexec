/**
 * @file include/bexec/awaitable.hpp
 * @brief Sender-to-awaitable coroutine bridge.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_AWAITABLE_HPP_
#define BEXEC_INCLUDE_BEXEC_AWAITABLE_HPP_

#include <bexec/detail/config.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/receiver.hpp>
#include <bexec/sender.hpp>
#include <cassert>
#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace bexec {

template <class Sender, class Promise>
class sender_awaitable;

struct as_awaitable_t;

namespace detail {

template <class T>
concept has_member_operator_co_await =
    requires(T&& value) { std::forward<T>(value).operator co_await(); };

template <class T>
concept has_adl_operator_co_await =
    requires(T&& value) { operator co_await(std::forward<T>(value)); };

template <class T>
decltype(auto) get_awaiter(T&& value) {
  if constexpr (has_member_operator_co_await<T>) {
    return std::forward<T>(value).operator co_await();
  } else if constexpr (has_adl_operator_co_await<T>) {
    return operator co_await(std::forward<T>(value));
  } else {
    return std::forward<T>(value);
  }
}

template <class Awaiter, class Promise>
concept awaiter_for =
    requires(Awaiter&& awaiter, std::coroutine_handle<Promise> handle) {
      { awaiter.await_ready() } -> std::convertible_to<bool>;
      awaiter.await_suspend(handle);
      awaiter.await_resume();
    };

template <class Value, class Promise>
concept awaitable_for =
    awaiter_for<decltype(detail::get_awaiter(std::declval<Value>())), Promise>;

template <class Value, class Promise>
concept has_member_as_awaitable = requires(Value&& value, Promise& promise) {
  std::forward<Value>(value).as_awaitable(promise);
};

template <class List>
struct awaitable_value_traits {
  static constexpr bool valid = false;
};

template <>
struct awaitable_value_traits<type_list<>> {
  static constexpr bool valid = true;
  using type = void;
};

template <>
struct awaitable_value_traits<type_list<std::tuple<>>> {
  static constexpr bool valid = true;
  using type = void;
};

template <class T>
struct awaitable_value_traits<type_list<std::tuple<T>>> {
  static constexpr bool valid = true;
  using type = T;
};

template <class Sender, class Promise>
using sender_awaitable_env_t =
    decltype(bexec::get_env(std::declval<Promise&>()));

template <class Sender, class Promise>
using sender_awaitable_value_traits_t = awaitable_value_traits<
    sender_value_tuple_list_t<Sender, sender_awaitable_env_t<Sender, Promise>>>;

template <class Sender, class Promise>
inline constexpr bool sender_awaitable_value_valid_v =
    sender_awaitable_value_traits_t<Sender, Promise>::valid;

template <class Sender, class Promise>
using sender_awaitable_value_t =
    typename sender_awaitable_value_traits_t<Sender, Promise>::type;

template <class Sender, class Promise>
concept awaitable_sender_for =
    sender_in<Sender, sender_awaitable_env_t<Sender, Promise>> &&
    sender_awaitable_value_valid_v<Sender, Promise> &&
    requires(Promise& promise) {
      {
        promise.unhandled_stopped()
      } -> std::convertible_to<std::coroutine_handle<>>;
    };

template <class T>
class awaitable_value_storage {
 public:
  template <class Value>
  void emplace(Value&& value) {
    value_.emplace(std::forward<Value>(value));
  }

  T take() {
    assert(value_.has_value());
    return std::move(*value_);
  }

 private:
  std::optional<T> value_;
};

template <>
class awaitable_value_storage<void> {
 public:
  void emplace() noexcept {}
  void take() const noexcept {}
};

}  // namespace detail

/**
 * @brief Awaiter that owns a sender operation inside the coroutine frame.
 *
 * The receiver stores only a pointer back to this awaiter. The operation is
 * constructed directly as a data member, so non-movable operation states are
 * supported without separate allocation or manual lifetime management.
 */
template <class Sender, class Promise>
class sender_awaitable {
  static_assert(detail::awaitable_sender_for<Sender, Promise>,
                "sender_awaitable requires no set_value completion or exactly "
                "one set_value() / set_value(T) completion");

  using value_type = detail::sender_awaitable_value_t<Sender, Promise>;

  class receiver {
   public:
    explicit receiver(sender_awaitable& owner) noexcept : owner_(&owner) {}

    [[nodiscard]] decltype(auto) get_env() const noexcept {
      return bexec::get_env(*owner_->promise_);
    }

    void set_value() noexcept
      requires std::is_void_v<value_type>
    {
      owner_->complete_value();
    }

    template <class Value>
      requires(!std::is_void_v<value_type> &&
               std::constructible_from<value_type, Value>)
    void set_value(Value&& value) noexcept {
      owner_->complete_value(std::forward<Value>(value));
    }

    template <class Error>
    void set_error(Error&& error) noexcept {
      owner_->complete_error(std::forward<Error>(error));
    }

    void set_stopped() noexcept { owner_->complete_stopped(); }

   private:
    sender_awaitable* owner_;
  };

  using operation_type = decltype(bexec::connect(std::declval<Sender&&>(),
                                                 std::declval<receiver>()));

 public:
  sender_awaitable(Sender&& sender, Promise& promise)
      : promise_(&promise),
        operation_(
            bexec::connect(std::forward<Sender>(sender), receiver{*this})) {}

  sender_awaitable(const sender_awaitable&) = delete;
  sender_awaitable& operator=(const sender_awaitable&) = delete;
  sender_awaitable(sender_awaitable&&) = delete;
  sender_awaitable& operator=(sender_awaitable&&) = delete;

  [[nodiscard]] bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> continuation) noexcept {
    continuation_ = continuation;
    bexec::start(operation_);
  }

  decltype(auto) await_resume() {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    if (error_) {
      std::rethrow_exception(error_);
    }
#else
    assert(!error_);
#endif

    if constexpr (std::is_void_v<value_type>) {
      value_.take();
      return;
    } else {
      return value_.take();
    }
  }

 private:
  void resume() noexcept { continuation_.resume(); }

  void complete_value() noexcept
    requires std::is_void_v<value_type>
  {
    value_.emplace();
    resume();
  }

  template <class Value>
    requires(!std::is_void_v<value_type> &&
             std::constructible_from<value_type, Value>)
  void complete_value(Value&& value) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      value_.emplace(std::forward<Value>(value));
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      error_ = std::current_exception();
    }
#endif
    resume();
  }

  template <class Error>
  void complete_error(Error&& error) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
      if constexpr (std::same_as<std::decay_t<Error>, std::exception_ptr>) {
        error_ = std::forward<Error>(error);
      } else {
        error_ = std::make_exception_ptr(std::forward<Error>(error));
      }
    } catch (...) {
      error_ = std::current_exception();
    }
#else
    if constexpr (std::same_as<std::decay_t<Error>, std::exception_ptr>) {
      error_ = std::forward<Error>(error);
    } else {
      (void)error;
      assert(false);
      BEXEC_DETAIL_UNREACHABLE();
    }
#endif
    resume();
  }

  void complete_stopped() noexcept { promise_->unhandled_stopped().resume(); }

  Promise* promise_;
  detail::awaitable_value_storage<value_type> value_;
  std::exception_ptr error_;
  std::coroutine_handle<> continuation_;
  operation_type operation_;
};

/**
 * @brief Converts a value to a promise-aware awaitable when appropriate.
 */
struct as_awaitable_t {
  template <class Value, class Promise>
  decltype(auto) operator()(Value&& value, Promise& promise) const {
    if constexpr (detail::has_member_as_awaitable<Value, Promise>) {
      return std::forward<Value>(value).as_awaitable(promise);
    } else if constexpr (detail::awaitable_for<Value, Promise>) {
      return std::forward<Value>(value);
    } else if constexpr (detail::awaitable_sender_for<Value, Promise>) {
      return sender_awaitable<Value, Promise>{std::forward<Value>(value),
                                              promise};
    } else {
      return std::forward<Value>(value);
    }
  }
};

inline constexpr as_awaitable_t as_awaitable{};

/**
 * @brief Promise mixin that enables co_await of bexec senders.
 */
template <class Promise>
class with_awaitable_senders {
 public:
  template <class Value>
  decltype(auto) await_transform(Value&& value) {
    return bexec::as_awaitable(std::forward<Value>(value),
                               static_cast<Promise&>(*this));
  }

  template <class OtherPromise>
    requires(!std::same_as<OtherPromise, void>)
  void set_continuation(
      std::coroutine_handle<OtherPromise> continuation) noexcept {
    continuation_ = continuation;
    if constexpr (requires(OtherPromise& promise) {
                    {
                      promise.unhandled_stopped()
                    } -> std::convertible_to<std::coroutine_handle<>>;
                  }) {
      stopped_handler_ = [](void* address) noexcept -> std::coroutine_handle<> {
        auto handle =
            std::coroutine_handle<OtherPromise>::from_address(address);
        return static_cast<std::coroutine_handle<>>(
            handle.promise().unhandled_stopped());
      };
    } else {
      stopped_handler_ = &default_unhandled_stopped;
    }
  }

  [[nodiscard]] std::coroutine_handle<> continuation() const noexcept {
    return continuation_;
  }

  [[nodiscard]] std::coroutine_handle<> unhandled_stopped() noexcept {
    return stopped_handler_(continuation_.address());
  }

 private:
  [[noreturn]] static std::coroutine_handle<> default_unhandled_stopped(
      void*) noexcept {
    assert(false);
    BEXEC_DETAIL_UNREACHABLE();
  }

  using stopped_handler_type = std::coroutine_handle<> (*)(void*) noexcept;

  std::coroutine_handle<> continuation_{std::noop_coroutine()};
  stopped_handler_type stopped_handler_{&default_unhandled_stopped};
};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_AWAITABLE_HPP_
