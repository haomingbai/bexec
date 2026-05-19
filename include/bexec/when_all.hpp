/**
 * @file include/bexec/when_all.hpp
 * @brief Public when_all sender algorithms.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines when_all and when_all_with_variant. Successful when_all completion
 * sends the concatenated child values in argument order. Errors are delivered
 * as their original error type.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_WHEN_ALL_HPP_
#define BEXEC_INCLUDE_BEXEC_WHEN_ALL_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/concepts.hpp>
#include <bexec/detail/config.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/detail/when_all.hpp>
#include <bexec/into_variant.hpp>
#include <bexec/receiver.hpp>
#include <bexec/sender.hpp>
#include <concepts>
#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

namespace bexec {

/**
 * @brief Sender that waits for all child senders.
 */
template <class... Senders>
class when_all_sender {
  static_assert(sizeof...(Senders) != 0,
                "when_all requires at least one sender");
  static_assert(((detail::sender_value_completion_count_v<Senders> <= 1U) &&
                 ...),
                "when_all requires each child sender to have at most one "
                "value completion; use when_all_with_variant for multiple "
                "value alternatives");

 public:
  using completion_signatures =
      detail::when_all_completion_signatures_t<Senders...>;
  using error_variant = detail::when_all_error_variant_t<Senders...>;
  using values_tuple = detail::when_all_values_tuple_t<Senders...>;
  static constexpr bool sends_value =
      (detail::sender_has_single_value_completion_v<Senders> && ...);

  explicit when_all_sender(Senders... senders)
      : senders_(std::move(senders)...) {}

  template <class Receiver>
  class operation {
   public:
    using sender_tuple = std::tuple<Senders...>;
    using state_type = detail::when_all_state<Receiver, error_variant,
                                              values_tuple, sends_value>;
    using indices = std::index_sequence_for<Senders...>;
    using operation_tuple =
        typename detail::when_all_operation_tuple<Receiver, error_variant,
                                                  values_tuple, sends_value,
                                                  sender_tuple, indices>::type;

    operation(sender_tuple senders, Receiver receiver)
        : senders_(std::move(senders)),
          state_(std::move(receiver), sizeof...(Senders)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    void start() noexcept { start_all(indices{}); }

   private:
    template <std::size_t... Indices>
    void start_all(std::index_sequence<Indices...>) noexcept {
      (start_one<Indices>(), ...);
    }

    template <std::size_t Index>
    void start_one() noexcept {
      using child_receiver = detail::when_all_child_receiver<Index, state_type>;
      using child_operation =
          detail::when_all_child_operation_t<Receiver, error_variant,
                                             values_tuple, sends_value,
                                             sender_tuple, Index>;
      auto& slot = std::get<Index>(operations_);

#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      try {
#endif
        slot.emplace_from([this]() -> child_operation {
          return bexec::connect(std::move(std::get<Index>(senders_)),
                                child_receiver{state_});
        });
        bexec::start(*slot);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      } catch (...) {
        state_.child_error(std::current_exception());
      }
#endif
    }

    sender_tuple senders_;
    state_type state_;
    operation_tuple operations_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return operation<Receiver>{std::move(senders_), std::move(receiver)};
  }

  template <class Receiver>
    requires((std::copy_constructible<Senders> && ...))
  auto connect(Receiver receiver) const& {
    return operation<Receiver>{senders_, std::move(receiver)};
  }

 private:
  std::tuple<Senders...> senders_;
};

/**
 * @brief Function object that creates when_all senders.
 */
struct when_all_t {
  template <sender... Senders>
    requires(sizeof...(Senders) != 0 &&
             ((detail::sender_value_completion_count_v<
                   detail::remove_cvref_t<Senders>> <= 1U) &&
              ...))
  [[nodiscard]] auto operator()(Senders&&... senders) const {
    return when_all_sender<detail::remove_cvref_t<Senders>...>{
        std::forward<Senders>(senders)...};
  }
};

/**
 * @brief Function object that creates when_all(into_variant(sender)...).
 */
struct when_all_with_variant_t {
  template <sender... Senders>
    requires(sizeof...(Senders) != 0)
  [[nodiscard]] auto operator()(Senders&&... senders) const {
    return when_all_t{}(bexec::into_variant(std::forward<Senders>(senders))...);
  }
};

inline constexpr when_all_t when_all{};
inline constexpr when_all_with_variant_t when_all_with_variant{};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_WHEN_ALL_HPP_
