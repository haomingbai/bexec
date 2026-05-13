/**
 * @file include/bexec/then.hpp
 * @brief Public then sender adaptor.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines the pipeable and direct then adaptor that transforms set_value
 * completions through a callable while forwarding error and stopped
 * completions.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_THEN_HPP_
#define BEXEC_INCLUDE_BEXEC_THEN_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/concepts.hpp>
#include <bexec/detail/operation.hpp>
#include <bexec/detail/then.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/sender.hpp>
#include <concepts>
#include <type_traits>
#include <utility>

namespace bexec {

template <class Sender, class Fn>
class then_sender;

template <class Fn>
class then_closure {
 public:
  explicit then_closure(Fn fn) : fn_(std::move(fn)) {}

  template <sender Sender>
  [[nodiscard]] auto operator()(Sender&& sender) const& {
    return then_sender<detail::remove_cvref_t<Sender>, Fn>{
        std::forward<Sender>(sender), fn_};
  }

  template <sender Sender>
  [[nodiscard]] auto operator()(Sender&& sender) && {
    return then_sender<detail::remove_cvref_t<Sender>, Fn>{
        std::forward<Sender>(sender), std::move(fn_)};
  }

 private:
  Fn fn_;
};

template <sender Sender, class Fn>
[[nodiscard]] auto operator|(Sender&& sender, then_closure<Fn> closure) {
  return std::move(closure)(std::forward<Sender>(sender));
}

/**
 * @brief Sender adaptor that transforms set_value through a callable.
 */
template <class Sender, class Fn>
class then_sender {
 public:
  using completion_signatures =
      detail::then_completion_signatures_t<Fn, Sender>;

  then_sender(Sender sender, Fn fn)
      : sender_(std::move(sender)), fn_(std::move(fn)) {}

  template <class Receiver>
  auto connect(Receiver receiver) && {
    using wrapped_type = detail::then_receiver<Receiver, Fn>;
    using operation_type = decltype(bexec::connect(
        std::declval<Sender>(), std::declval<wrapped_type>()));

    return detail::pass_through_operation<operation_type>{
        std::in_place, [sender = std::move(sender_), fn = std::move(fn_),
                        receiver = std::move(receiver)]() mutable {
          auto wrapped = wrapped_type{std::move(receiver), std::move(fn)};
          return bexec::connect(std::move(sender), std::move(wrapped));
        }};
  }

  template <class Receiver>
    requires std::copy_constructible<Sender> && std::copy_constructible<Fn>
  auto connect(Receiver receiver) const& {
    using wrapped_type = detail::then_receiver<Receiver, Fn>;
    using operation_type = decltype(bexec::connect(
        std::declval<const Sender&>(), std::declval<wrapped_type>()));

    return detail::pass_through_operation<operation_type>{
        std::in_place, [this, receiver = std::move(receiver)]() mutable {
          auto wrapped = wrapped_type{std::move(receiver), fn_};
          return bexec::connect(sender_, std::move(wrapped));
        }};
  }

 private:
  Sender sender_;
  Fn fn_;
};

/**
 * @brief Creates a pipeable sender adaptor that transforms set_value.
 */
template <class Fn>
[[nodiscard]] auto then(Fn&& fn) {
  return then_closure<std::decay_t<Fn>>{std::forward<Fn>(fn)};
}

/**
 * @brief Applies then directly to a sender.
 */
template <sender Sender, class Fn>
[[nodiscard]] auto then(Sender&& sender, Fn&& fn) {
  return then_sender<detail::remove_cvref_t<Sender>, std::decay_t<Fn>>{
      std::forward<Sender>(sender), std::forward<Fn>(fn)};
}

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_THEN_HPP_
