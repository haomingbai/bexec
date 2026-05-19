/**
 * @file include/bexec/into_variant.hpp
 * @brief Public into_variant sender adaptor.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_INTO_VARIANT_HPP_
#define BEXEC_INCLUDE_BEXEC_INTO_VARIANT_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/concepts.hpp>
#include <bexec/detail/config.hpp>
#include <bexec/detail/operation.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/receiver.hpp>
#include <bexec/sender.hpp>
#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace bexec {

namespace detail {

template <class Receiver, class ValueVariant>
class into_variant_receiver {
 public:
  explicit into_variant_receiver(Receiver receiver)
      : receiver_(std::move(receiver)) {}

  [[nodiscard]] auto get_env() const
      noexcept(noexcept(bexec::get_env(receiver_))) {
    return bexec::get_env(receiver_);
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      using tuple_type = decayed_tuple<Args...>;
      bexec::set_value(std::move(receiver_),
                       ValueVariant{std::in_place_type<tuple_type>,
                                    std::forward<Args>(args)...});
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      bexec::set_error(std::move(receiver_), std::current_exception());
    }
#endif
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    bexec::set_error(std::move(receiver_), std::forward<Error>(error));
  }

  void set_stopped() noexcept { bexec::set_stopped(std::move(receiver_)); }

 private:
  Receiver receiver_;
};

}  // namespace detail

template <class Sender>
class into_variant_sender {
 public:
  using completion_signatures =
      detail::into_variant_completion_signatures_t<Sender>;
  using value_variant = detail::into_variant_value_variant_t<Sender>;

  template <class SenderArg>
    requires std::constructible_from<Sender, SenderArg>
  explicit into_variant_sender(SenderArg&& sender)
      : sender_(std::forward<SenderArg>(sender)) {}

  template <class Receiver>
  auto connect(Receiver receiver) && {
    using wrapped_type = detail::into_variant_receiver<Receiver, value_variant>;
    using operation_type = decltype(bexec::connect(
        std::declval<Sender>(), std::declval<wrapped_type>()));

    return detail::pass_through_operation<operation_type>{
        std::in_place, [sender = std::move(sender_),
                        receiver = std::move(receiver)]() mutable {
          return bexec::connect(std::move(sender),
                                wrapped_type{std::move(receiver)});
        }};
  }

  template <class Receiver>
    requires std::copy_constructible<Sender>
  auto connect(Receiver receiver) const& {
    using wrapped_type = detail::into_variant_receiver<Receiver, value_variant>;
    using operation_type = decltype(bexec::connect(
        std::declval<const Sender&>(), std::declval<wrapped_type>()));

    return detail::pass_through_operation<operation_type>{
        std::in_place, [this, receiver = std::move(receiver)]() mutable {
          return bexec::connect(sender_, wrapped_type{std::move(receiver)});
        }};
  }

 private:
  Sender sender_;
};

struct into_variant_closure {
  template <sender Sender>
    requires(detail::sender_value_completion_count_v<
                 detail::remove_cvref_t<Sender>> != 0U)
  [[nodiscard]] auto operator()(Sender&& sender) const {
    return into_variant_sender<detail::remove_cvref_t<Sender>>{
        std::forward<Sender>(sender)};
  }
};

template <sender Sender>
  requires(
      detail::sender_value_completion_count_v<detail::remove_cvref_t<Sender>> !=
      0U)
[[nodiscard]] auto operator|(Sender&& sender, into_variant_closure closure) {
  return closure(std::forward<Sender>(sender));
}

/**
 * @brief Function object that creates or applies into_variant adaptors.
 */
struct into_variant_t {
  [[nodiscard]] into_variant_closure operator()() const { return {}; }

  template <sender Sender>
    requires(detail::sender_value_completion_count_v<
                 detail::remove_cvref_t<Sender>> != 0U)
  [[nodiscard]] auto operator()(Sender&& sender) const {
    return into_variant_closure{}(std::forward<Sender>(sender));
  }
};

inline constexpr into_variant_t into_variant{};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_INTO_VARIANT_HPP_
