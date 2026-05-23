/**
 * @file include/bexec/sender.hpp
 * @brief Sender customization point, metadata helpers, and concepts.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines bexec::connect, completion metadata extraction, value/error/stopped
 * introspection helpers, and sender/sender_to concepts.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_SENDER_HPP_
#define BEXEC_INCLUDE_BEXEC_SENDER_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/env.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/receiver.hpp>
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

namespace bexec {

/**
 * @brief Connects a sender to a receiver by calling sender.connect(receiver).
 */
struct connect_t {
  template <class Sender, class Receiver>
    requires requires(Sender&& sender, Receiver&& receiver) {
      std::forward<Sender>(sender).connect(std::forward<Receiver>(receiver));
    }
  constexpr decltype(auto) operator()(Sender&& sender,
                                      Receiver&& receiver) const
      noexcept(noexcept(std::forward<Sender>(sender).connect(
          std::forward<Receiver>(receiver)))) {
    return std::forward<Sender>(sender).connect(
        std::forward<Receiver>(receiver));
  }
};

inline constexpr connect_t connect{};

template <class Sender, class Env = empty_env,
          template <class...> class Tuple = std::tuple,
          template <class...> class Variant = variant_or_empty>
using value_types_of_t = detail::gather_signatures_t<
    set_value_t, completion_signatures_of_t<Sender, Env>, Tuple, Variant>;

template <class Sender, class Env = empty_env,
          template <class...> class Variant = variant_or_empty>
using error_types_of_t =
    detail::gather_signatures_t<set_error_t,
                                completion_signatures_of_t<Sender, Env>,
                                bexec::single_type, Variant>;

template <class Sender, class Env = empty_env>
inline constexpr bool sends_stopped =
    (completion_signatures_of_t<Sender,
                                Env>::template count_of<set_stopped_t>() != 0);

/**
 * @brief Concept for sender-like types that publish bexec completion metadata.
 */
template <class Sender>
concept sender =
    std::move_constructible<detail::remove_cvref_t<Sender>> && requires {
      typename bexec::completion_signatures_of_t<Sender>;
    } && valid_completion_signatures<bexec::completion_signatures_of_t<Sender>>;

template <class Sender, class Env = empty_env>
concept sender_in =
    sender<Sender> &&
    valid_completion_signatures<bexec::completion_signatures_of_t<Sender, Env>>;

/**
 * @brief Concept for sender/receiver pairs connectable through bexec::connect.
 */
template <class Sender, class Receiver>
concept sender_to =
    sender_in<Sender, decltype(bexec::get_env(std::declval<Receiver&>()))> &&
    receiver_of<Receiver, completion_signatures_of_t<
                              Sender, decltype(bexec::get_env(
                                          std::declval<Receiver&>()))>> &&
    requires(Sender&& sender, Receiver&& receiver) {
      {
        bexec::connect(std::forward<Sender>(sender),
                       std::forward<Receiver>(receiver))
      } -> operation_state;
    };

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_SENDER_HPP_
