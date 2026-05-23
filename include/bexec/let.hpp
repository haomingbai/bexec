/**
 * @file include/bexec/let.hpp
 * @brief Public let sender adaptors.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines pipeable and direct let_value, let_error, and let_stopped adaptors.
 * A selected upstream completion invokes a callable that returns a child
 * sender; non-selected completions are forwarded unchanged.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_LET_HPP_
#define BEXEC_INCLUDE_BEXEC_LET_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/concepts.hpp>
#include <bexec/detail/let.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/sender.hpp>
#include <concepts>
#include <type_traits>
#include <utility>

namespace bexec {

template <class Tag, class Sender, class Fn>
class let_sender;

template <class Tag, class Fn>
class let_closure {
 public:
  template <class FnArg>
    requires std::constructible_from<Fn, FnArg>
  explicit let_closure(FnArg&& fn) : fn_(std::forward<FnArg>(fn)) {}

  template <sender Sender>
  [[nodiscard]] auto operator()(Sender&& sender) const& {
    return let_sender<Tag, detail::remove_cvref_t<Sender>, Fn>{
        std::forward<Sender>(sender), fn_};
  }

  template <sender Sender>
  [[nodiscard]] auto operator()(Sender&& sender) && {
    return let_sender<Tag, detail::remove_cvref_t<Sender>, Fn>{
        std::forward<Sender>(sender), std::move(fn_)};
  }

 private:
  Fn fn_;
};

template <sender Sender, class Tag, class Fn>
[[nodiscard]] auto operator|(Sender&& sender, let_closure<Tag, Fn> closure) {
  return std::move(closure)(std::forward<Sender>(sender));
}

/**
 * @brief Sender adaptor that replaces one completion kind with a child sender.
 */
template <class Tag, class Sender, class Fn>
class let_sender {
 public:
  using completion_signatures =
      detail::let_completion_signatures_t<Tag, Fn, Sender>;

  template <class Self, class Env>
  [[nodiscard]] static consteval auto get_completion_signatures() {
    return detail::let_completion_signatures_for_env_t<Tag, Fn, Sender, Env>{};
  }

  template <class SenderArg, class FnArg>
    requires std::constructible_from<Sender, SenderArg> &&
                 std::constructible_from<Fn, FnArg>
  let_sender(SenderArg&& sender, FnArg&& fn)
      : sender_(std::forward<SenderArg>(sender)),
        fn_(std::forward<FnArg>(fn)) {}

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return detail::let_operation<Tag, Sender, Fn, Receiver>{
        std::move(sender_), std::move(fn_), std::move(receiver)};
  }

  template <class Receiver>
    requires std::copy_constructible<Sender> && std::copy_constructible<Fn>
  auto connect(Receiver receiver) const& {
    return detail::let_operation<Tag, const Sender&, Fn, Receiver>{
        sender_, fn_, std::move(receiver)};
  }

 private:
  Sender sender_;
  Fn fn_;
};

/**
 * @brief Function object that creates or applies let_value adaptors.
 */
struct let_value_t {
  template <class Fn>
  [[nodiscard]] auto operator()(Fn&& fn) const {
    return let_closure<set_value_t, std::decay_t<Fn>>{std::forward<Fn>(fn)};
  }

  template <sender Sender, class Fn>
  [[nodiscard]] auto operator()(Sender&& sender, Fn&& fn) const {
    return let_sender<set_value_t, detail::remove_cvref_t<Sender>,
                      std::decay_t<Fn>>{std::forward<Sender>(sender),
                                        std::forward<Fn>(fn)};
  }
};

/**
 * @brief Function object that creates or applies let_error adaptors.
 */
struct let_error_t {
  template <class Fn>
  [[nodiscard]] auto operator()(Fn&& fn) const {
    return let_closure<set_error_t, std::decay_t<Fn>>{std::forward<Fn>(fn)};
  }

  template <sender Sender, class Fn>
  [[nodiscard]] auto operator()(Sender&& sender, Fn&& fn) const {
    return let_sender<set_error_t, detail::remove_cvref_t<Sender>,
                      std::decay_t<Fn>>{std::forward<Sender>(sender),
                                        std::forward<Fn>(fn)};
  }
};

/**
 * @brief Function object that creates or applies let_stopped adaptors.
 */
struct let_stopped_t {
  template <class Fn>
  [[nodiscard]] auto operator()(Fn&& fn) const {
    return let_closure<set_stopped_t, std::decay_t<Fn>>{std::forward<Fn>(fn)};
  }

  template <sender Sender, class Fn>
  [[nodiscard]] auto operator()(Sender&& sender, Fn&& fn) const {
    return let_sender<set_stopped_t, detail::remove_cvref_t<Sender>,
                      std::decay_t<Fn>>{std::forward<Sender>(sender),
                                        std::forward<Fn>(fn)};
  }
};

inline constexpr let_value_t let_value{};
inline constexpr let_error_t let_error{};
inline constexpr let_stopped_t let_stopped{};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_LET_HPP_
