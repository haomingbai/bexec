/**
 * @file include/bexec/detail/let.hpp
 * @brief Internal operation state for let sender adaptors.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements the common state machine for let_value, let_error, and
 * let_stopped. The selected upstream completion invokes a callable that
 * returns a child sender; non-selected completions are forwarded unchanged.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_LET_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_LET_HPP_

#include <bexec/detail/config.hpp>
#include <bexec/detail/manual_lifetime.hpp>
#include <bexec/detail/operation_storage.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/receiver.hpp>
#include <bexec/sender.hpp>
#include <concepts>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace bexec::detail {

template <class Tag, class Sender, class Fn, class Receiver>
class let_operation;

template <class Operation, class Receiver>
class let_child_receiver {
 public:
  explicit let_child_receiver(Operation& parent) : parent_(&parent) {}

  [[nodiscard]] auto get_env() const
      noexcept(noexcept(bexec::get_env(std::declval<Receiver&>())))
          -> decltype(bexec::get_env(std::declval<Receiver&>())) {
    return bexec::get_env(parent_->receiver());
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
    parent_->child_value(std::forward<Args>(args)...);
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    parent_->child_error(std::forward<Error>(error));
  }

  void set_stopped() noexcept { parent_->child_stopped(); }

 private:
  Operation* parent_;
};

template <class Tag, class Fn, class ChildReceiver, class Signature>
struct let_child_operation_type;

template <class Fn, class ChildReceiver, class... Args>
struct let_child_operation_type<set_value_t, Fn, ChildReceiver,
                                set_value_t(Args...)> {
  using sender_type = std::invoke_result_t<Fn&, Args...>;
  using type = decltype(bexec::connect(std::declval<sender_type>(),
                                       std::declval<ChildReceiver>()));
};

template <class Fn, class ChildReceiver, class Error>
struct let_child_operation_type<set_error_t, Fn, ChildReceiver,
                                set_error_t(Error)> {
  using sender_type = std::invoke_result_t<Fn&, Error>;
  using type = decltype(bexec::connect(std::declval<sender_type>(),
                                       std::declval<ChildReceiver>()));
};

template <class Fn, class ChildReceiver>
struct let_child_operation_type<set_stopped_t, Fn, ChildReceiver,
                                set_stopped_t()> {
  using sender_type = std::invoke_result_t<Fn&>;
  using type = decltype(bexec::connect(std::declval<sender_type>(),
                                       std::declval<ChildReceiver>()));
};

template <class Tag, class Fn, class ChildReceiver, class Signature>
struct let_child_operation_for_signature {
  using type = type_list<>;
};

template <class Fn, class ChildReceiver, class... Args>
struct let_child_operation_for_signature<set_value_t, Fn, ChildReceiver,
                                         set_value_t(Args...)> {
  using type = type_list<typename let_child_operation_type<
      set_value_t, Fn, ChildReceiver, set_value_t(Args...)>::type>;
};

template <class Fn, class ChildReceiver, class Error>
struct let_child_operation_for_signature<set_error_t, Fn, ChildReceiver,
                                         set_error_t(Error)> {
  using type = type_list<typename let_child_operation_type<
      set_error_t, Fn, ChildReceiver, set_error_t(Error)>::type>;
};

template <class Fn, class ChildReceiver>
struct let_child_operation_for_signature<set_stopped_t, Fn, ChildReceiver,
                                         set_stopped_t()> {
  using type = type_list<typename let_child_operation_type<
      set_stopped_t, Fn, ChildReceiver, set_stopped_t()>::type>;
};

template <class Tag, class Fn, class ChildReceiver, class Completions>
struct let_child_operation_list;

template <class Tag, class Fn, class ChildReceiver, class... Signatures>
struct let_child_operation_list<Tag, Fn, ChildReceiver,
                                completion_signatures<Signatures...>> {
  using gathered =
      concat_type_lists_t<typename let_child_operation_for_signature<
          Tag, Fn, ChildReceiver, Signatures>::type...>;
  using type = unique_type_list_t<gathered>;
};

template <class Tag, class Fn, class ChildReceiver, class Completions>
using let_child_operation_list_t =
    typename let_child_operation_list<Tag, Fn, ChildReceiver,
                                      Completions>::type;

template <class Tag, class Operation, class Receiver>
class let_receiver {
 public:
  explicit let_receiver(Operation& parent) : parent_(&parent) {}

  [[nodiscard]] auto get_env() const
      noexcept(noexcept(bexec::get_env(std::declval<Receiver&>())))
          -> decltype(bexec::get_env(std::declval<Receiver&>())) {
    return bexec::get_env(parent_->receiver());
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
    if constexpr (std::same_as<Tag, set_value_t>) {
      parent_->start_child_value(std::forward<Args>(args)...);
    } else {
      parent_->forward_value(std::forward<Args>(args)...);
    }
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    if constexpr (std::same_as<Tag, set_error_t>) {
      parent_->start_child_error(std::forward<Error>(error));
    } else {
      parent_->forward_error(std::forward<Error>(error));
    }
  }

  void set_stopped() noexcept {
    if constexpr (std::same_as<Tag, set_stopped_t>) {
      parent_->start_child_stopped();
    } else {
      parent_->forward_stopped();
    }
  }

 private:
  Operation* parent_;
};

template <class Tag, class Sender, class Fn, class Receiver>
class let_operation {
 public:
  using operation_type = let_operation;
  using upstream_receiver_type = let_receiver<Tag, operation_type, Receiver>;
  using upstream_operation_type = decltype(bexec::connect(
      std::declval<Sender>(), std::declval<upstream_receiver_type>()));
  using child_receiver_type = let_child_receiver<operation_type, Receiver>;
  using child_operation_list =
      let_child_operation_list_t<Tag, Fn, child_receiver_type,
                                 sender_completion_signatures_t<Sender>>;
  using child_operations_type = operation_storage<child_operation_list>;

  template <class SenderArg, class FnArg>
  let_operation(SenderArg&& sender, FnArg&& fn, Receiver receiver)
      : fn_(std::forward<FnArg>(fn)), receiver_(std::move(receiver)) {
    upstream_.emplace_from([this, &sender]() -> upstream_operation_type {
      return bexec::connect(std::forward<SenderArg>(sender),
                            upstream_receiver_type{*this});
    });
  }

  let_operation(const let_operation&) = delete;
  let_operation& operator=(const let_operation&) = delete;
  let_operation(let_operation&&) = delete;
  let_operation& operator=(let_operation&&) = delete;

  Receiver& receiver() noexcept { return receiver_; }

  void start() noexcept { bexec::start(*upstream_); }

  template <class... Args>
  void start_child_value(Args&&... args) noexcept {
    start_child<set_value_t(Args...)>(std::forward<Args>(args)...);
  }

  template <class Error>
  void start_child_error(Error&& error) noexcept {
    start_child<set_error_t(Error)>(std::forward<Error>(error));
  }

  void start_child_stopped() noexcept { start_child<set_stopped_t()>(); }

  template <class... Args>
  void forward_value(Args&&... args) noexcept {
    bexec::set_value(std::move(receiver_), std::forward<Args>(args)...);
  }

  template <class Error>
  void forward_error(Error&& error) noexcept {
    bexec::set_error(std::move(receiver_), std::forward<Error>(error));
  }

  void forward_stopped() noexcept { bexec::set_stopped(std::move(receiver_)); }

  template <class... Args>
  void child_value(Args&&... args) noexcept {
    bexec::set_value(std::move(receiver_), std::forward<Args>(args)...);
  }

  template <class Error>
  void child_error(Error&& error) noexcept {
    bexec::set_error(std::move(receiver_), std::forward<Error>(error));
  }

  void child_stopped() noexcept { bexec::set_stopped(std::move(receiver_)); }

 private:
  template <class Signature, class... Args>
  void start_child(Args&&... args) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      using child_operation =
          typename let_child_operation_type<Tag, Fn, child_receiver_type,
                                            Signature>::type;

      child_operations_.template emplace_from<child_operation>(
          [this, &args...]() -> child_operation {
            return bexec::connect(std::invoke(fn_, std::forward<Args>(args)...),
                                  child_receiver_type{*this});
          });
      child_operations_.start();
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      bexec::set_error(std::move(receiver_), std::current_exception());
    }
#endif
  }

  Fn fn_;
  Receiver receiver_;
  manual_lifetime<upstream_operation_type> upstream_;
  child_operations_type child_operations_;
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_LET_HPP_
