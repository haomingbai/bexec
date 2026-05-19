/**
 * @file include/bexec/detail/when_all.hpp
 * @brief Internal shared-state helpers for when_all.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_WHEN_ALL_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_WHEN_ALL_HPP_

#include <bexec/detail/config.hpp>
#include <bexec/detail/manual_lifetime.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/env.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/receiver.hpp>
#include <bexec/sender.hpp>
#include <exception>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace bexec::detail {

template <class Sender,
          bool HasValue = sender_has_single_value_completion_v<Sender>>
struct when_all_value_slot {
  using type = std::monostate;
};

template <class Sender>
struct when_all_value_slot<Sender, true> {
  using type = std::optional<sender_single_value_tuple_t<Sender>>;
};

template <class Sender>
using when_all_value_slot_t = typename when_all_value_slot<Sender>::type;

template <class... Senders>
using when_all_values_tuple_t = std::tuple<when_all_value_slot_t<Senders>...>;

template <class... Senders>
using when_all_error_list_t =
    unique_type_list_t<concat_type_lists_t<sender_error_types_t<Senders>...,
                                           type_list<std::exception_ptr>>>;

template <class... Senders>
using when_all_error_variant_t =
    variant_from_type_list_t<when_all_error_list_t<Senders...>>;

template <class... Senders>
using when_all_error_signature_list_t =
    set_error_signatures_from_type_list_t<when_all_error_list_t<Senders...>>;

template <class... Senders>
using when_all_stopped_signature_list_t =
    std::conditional_t<(sender_sends_stopped_v<Senders> || ...),
                       type_list<set_stopped_t()>, type_list<>>;

template <class... Senders>
struct when_all_completion_signatures {
  using signatures = unique_type_list_t<
      concat_type_lists_t<when_all_value_signature_list_t<Senders...>,
                          when_all_error_signature_list_t<Senders...>,
                          when_all_stopped_signature_list_t<Senders...>>>;
  using type = completion_signatures_from_type_list_t<signatures>;
};

template <class... Senders>
using when_all_completion_signatures_t =
    typename when_all_completion_signatures<Senders...>::type;

template <class Receiver, class ErrorVariant, class ValuesTuple,
          bool SendsValue>
struct when_all_state {
  explicit when_all_state(Receiver recv, std::size_t count)
      : receiver(std::move(recv)), remaining(count) {}

  template <std::size_t Index, class... Args>
  void child_value(Args&&... args) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      {
        std::lock_guard lock(mutex);
        auto& slot = std::get<Index>(values);
        slot.emplace(std::forward<Args>(args)...);
      }
      finish_one();
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      child_error(std::current_exception());
    }
#endif
  }

  template <class Error>
  void child_error(Error&& error_value) noexcept {
    bool request_stop = false;
    {
      std::lock_guard lock(mutex);
      if (terminal == terminal_kind::none) {
        terminal = terminal_kind::error;
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
        try {
#endif
          store_error(std::forward<Error>(error_value));
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
        } catch (...) {
          error.emplace(std::in_place_type<std::exception_ptr>,
                        std::current_exception());
        }
#endif
        request_stop = true;
      }
    }
    if (request_stop) {
      stop_source.request_stop();
    }
    finish_one();
  }

  void child_stopped() noexcept {
    bool request_stop = false;
    {
      std::lock_guard lock(mutex);
      if (terminal == terminal_kind::none) {
        terminal = terminal_kind::stopped;
        request_stop = true;
      }
    }
    if (request_stop) {
      stop_source.request_stop();
    }
    finish_one();
  }

  void finish_one() noexcept {
    std::optional<ErrorVariant> error_to_deliver;
    std::optional<Receiver> receiver_to_complete;
    std::optional<ValuesTuple> values_to_deliver;
    terminal_kind final_terminal = terminal_kind::none;

    {
      std::lock_guard lock(mutex);
      if (remaining == 0) {
        return;
      }
      --remaining;
      if (remaining != 0 || completed) {
        return;
      }

      completed = true;
      final_terminal = terminal;
      if (error) {
        error_to_deliver.emplace(std::move(*error));
      }
      if constexpr (SendsValue) {
        if (final_terminal == terminal_kind::none) {
          values_to_deliver.emplace(std::move(values));
        }
      }
      receiver_to_complete.emplace(std::move(receiver));
    }

    if (final_terminal == terminal_kind::error) {
      deliver_error(std::move(*receiver_to_complete),
                    std::move(*error_to_deliver));
    } else if (final_terminal == terminal_kind::stopped) {
      bexec::set_stopped(std::move(*receiver_to_complete));
    } else {
      if constexpr (SendsValue) {
        deliver_success(std::move(*receiver_to_complete),
                        std::move(*values_to_deliver));
      } else {
        bexec::set_value(std::move(*receiver_to_complete));
      }
    }
  }

  template <class Error>
  void store_error(Error&& error_value) noexcept {
    using error_type = std::decay_t<Error>;
    if constexpr (variant_contains_v<error_type, ErrorVariant>) {
      error.emplace(std::in_place_type<error_type>,
                    std::forward<Error>(error_value));
    } else {
      static_assert(dependent_false<Error>,
                    "when_all child sent an error type not listed in "
                    "completion signatures");
    }
  }

  static void deliver_error(Receiver recv, ErrorVariant error_value) noexcept {
    std::visit(
        [&recv](auto& error) noexcept {
          bexec::set_error(std::move(recv), std::move(error));
        },
        error_value);
  }

  static void deliver_success(Receiver recv, ValuesTuple values) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      auto joined = tuple_cat_values(
          std::move(values),
          std::make_index_sequence<std::tuple_size_v<ValuesTuple>>{});
      std::apply(
          [&recv](auto&&... args) noexcept {
            bexec::set_value(std::move(recv),
                             std::forward<decltype(args)>(args)...);
          },
          std::move(joined));
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      bexec::set_error(std::move(recv), std::current_exception());
    }
#endif
  }

  template <std::size_t... Indices>
  static auto tuple_cat_values(ValuesTuple values,
                               std::index_sequence<Indices...>) {
    return std::tuple_cat(std::move(*std::get<Indices>(values))...);
  }

  enum class terminal_kind { none, error, stopped };

  Receiver receiver;
  std::size_t remaining;
  std::mutex mutex;
  inplace_stop_source stop_source;
  terminal_kind terminal{terminal_kind::none};
  std::optional<ErrorVariant> error;
  ValuesTuple values;
  bool completed{false};
};

template <std::size_t Index, class State>
class when_all_child_receiver {
 public:
  explicit when_all_child_receiver(State& state) : state_(&state) {}

  [[nodiscard]] auto get_env() const noexcept {
    return env_with_stop_token{state_->stop_source.get_token(),
                               bexec::get_env(state_->receiver)};
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
    state_->template child_value<Index>(std::forward<Args>(args)...);
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    state_->child_error(std::forward<Error>(error));
  }

  void set_stopped() noexcept { state_->child_stopped(); }

 private:
  State* state_;
};

template <class Receiver, class ErrorVariant, class ValuesTuple,
          bool SendsValue, class SenderTuple, std::size_t Index>
using when_all_child_operation_t = decltype(bexec::connect(
    std::declval<std::tuple_element_t<Index, SenderTuple>>(),
    std::declval<when_all_child_receiver<
        Index,
        when_all_state<Receiver, ErrorVariant, ValuesTuple, SendsValue>>>()));

template <class Receiver, class ErrorVariant, class ValuesTuple,
          bool SendsValue, class SenderTuple, class Indices>
struct when_all_operation_tuple;

template <class Receiver, class ErrorVariant, class ValuesTuple,
          bool SendsValue, class SenderTuple, std::size_t... Indices>
struct when_all_operation_tuple<Receiver, ErrorVariant, ValuesTuple, SendsValue,
                                SenderTuple, std::index_sequence<Indices...>> {
  using type = std::tuple<manual_lifetime<
      when_all_child_operation_t<Receiver, ErrorVariant, ValuesTuple,
                                 SendsValue, SenderTuple, Indices>>...>;
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_WHEN_ALL_HPP_
