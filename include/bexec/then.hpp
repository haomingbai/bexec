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
    auto wrapped = detail::then_receiver<Receiver, Fn>{std::move(receiver),
                                                       std::move(fn_)};
    auto operation = bexec::connect(std::move(sender_), std::move(wrapped));
    return detail::pass_through_operation<decltype(operation)>{
        std::move(operation)};
  }

  template <class Receiver>
    requires std::copy_constructible<Sender> && std::copy_constructible<Fn>
  auto connect(Receiver receiver) const& {
    auto wrapped =
        detail::then_receiver<Receiver, Fn>{std::move(receiver), fn_};
    auto operation = bexec::connect(sender_, std::move(wrapped));
    return detail::pass_through_operation<decltype(operation)>{
        std::move(operation)};
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
