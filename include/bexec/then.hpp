/**
 * @file include/bexec/then.hpp
 * @brief Public completion-transforming sender adaptors.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines the pipeable and direct then, upon_error, and upon_stopped adaptors.
 * A selected completion invokes a callable and sends its result as a value;
 * non-selected completions are forwarded unchanged.
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

template <class Tag, class Sender, class Fn>
class completion_adaptor_sender;

template <class Tag, class Fn>
class completion_adaptor_closure {
 public:
  template <class FnArg>
    requires std::constructible_from<Fn, FnArg>
  explicit completion_adaptor_closure(FnArg&& fn)
      : fn_(std::forward<FnArg>(fn)) {}

  template <sender Sender>
  [[nodiscard]] auto operator()(Sender&& sender) const& {
    return completion_adaptor_sender<Tag, detail::remove_cvref_t<Sender>, Fn>{
        std::forward<Sender>(sender), fn_};
  }

  template <sender Sender>
  [[nodiscard]] auto operator()(Sender&& sender) && {
    return completion_adaptor_sender<Tag, detail::remove_cvref_t<Sender>, Fn>{
        std::forward<Sender>(sender), std::move(fn_)};
  }

 private:
  Fn fn_;
};

template <sender Sender, class Tag, class Fn>
[[nodiscard]] auto operator|(Sender&& sender,
                             completion_adaptor_closure<Tag, Fn> closure) {
  return std::move(closure)(std::forward<Sender>(sender));
}

/**
 * @brief Sender adaptor that transforms one completion kind through a callable.
 */
template <class Tag, class Sender, class Fn>
class completion_adaptor_sender {
 public:
  using completion_signatures =
      detail::completion_adaptor_completion_signatures_t<Tag, Fn, Sender>;

  template <class SenderArg, class FnArg>
    requires std::constructible_from<Sender, SenderArg> &&
                 std::constructible_from<Fn, FnArg>
  completion_adaptor_sender(SenderArg&& sender, FnArg&& fn)
      : sender_(std::forward<SenderArg>(sender)),
        fn_(std::forward<FnArg>(fn)) {}

  template <class Receiver>
  auto connect(Receiver receiver) && {
    using wrapped_type = detail::completion_adaptor_receiver<Tag, Receiver, Fn>;
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
    using wrapped_type = detail::completion_adaptor_receiver<Tag, Receiver, Fn>;
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

template <class Sender, class Fn>
using then_sender = completion_adaptor_sender<set_value_t, Sender, Fn>;

/**
 * @brief Function object that creates or applies then adaptors.
 */
struct then_t {
  template <class Fn>
  [[nodiscard]] auto operator()(Fn&& fn) const {
    return completion_adaptor_closure<set_value_t, std::decay_t<Fn>>{
        std::forward<Fn>(fn)};
  }

  template <sender Sender, class Fn>
  [[nodiscard]] auto operator()(Sender&& sender, Fn&& fn) const {
    return completion_adaptor_sender<
        set_value_t, detail::remove_cvref_t<Sender>, std::decay_t<Fn>>{
        std::forward<Sender>(sender), std::forward<Fn>(fn)};
  }
};

/**
 * @brief Function object that creates or applies upon_error adaptors.
 */
struct upon_error_t {
  template <class Fn>
  [[nodiscard]] auto operator()(Fn&& fn) const {
    return completion_adaptor_closure<set_error_t, std::decay_t<Fn>>{
        std::forward<Fn>(fn)};
  }

  template <sender Sender, class Fn>
  [[nodiscard]] auto operator()(Sender&& sender, Fn&& fn) const {
    return completion_adaptor_sender<
        set_error_t, detail::remove_cvref_t<Sender>, std::decay_t<Fn>>{
        std::forward<Sender>(sender), std::forward<Fn>(fn)};
  }
};

/**
 * @brief Function object that creates or applies upon_stopped adaptors.
 */
struct upon_stopped_t {
  template <class Fn>
  [[nodiscard]] auto operator()(Fn&& fn) const {
    return completion_adaptor_closure<set_stopped_t, std::decay_t<Fn>>{
        std::forward<Fn>(fn)};
  }

  template <sender Sender, class Fn>
  [[nodiscard]] auto operator()(Sender&& sender, Fn&& fn) const {
    return completion_adaptor_sender<
        set_stopped_t, detail::remove_cvref_t<Sender>, std::decay_t<Fn>>{
        std::forward<Sender>(sender), std::forward<Fn>(fn)};
  }
};

inline constexpr then_t then{};
inline constexpr upon_error_t upon_error{};
inline constexpr upon_stopped_t upon_stopped{};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_THEN_HPP_
