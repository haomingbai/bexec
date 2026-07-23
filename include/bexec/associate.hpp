/**
 * @file include/bexec/associate.hpp
 * @brief Scope association sender adaptor and scope-token concepts.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-07-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines the public associate(sender, token) pipeable sender adaptor. Its
 * associated sender and operation-state representations are intentionally
 * implementation details.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_ASSOCIATE_HPP_
#define BEXEC_INCLUDE_BEXEC_ASSOCIATE_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/detail/associate.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/sender.hpp>
#include <concepts>
#include <type_traits>
#include <utility>

namespace bexec {

namespace detail {

struct scope_token_test_sender {
  using completion_signatures = bexec::completion_signatures<set_value_t()>;
};

}  // namespace detail

/**
 * @brief Concept for an owning association obtained from a scope token.
 */
template <class Assoc>
concept scope_association =
    std::movable<detail::remove_cvref_t<Assoc>> &&
    std::is_nothrow_move_constructible_v<detail::remove_cvref_t<Assoc>> &&
    std::is_nothrow_move_assignable_v<detail::remove_cvref_t<Assoc>> &&
    std::is_nothrow_default_constructible_v<detail::remove_cvref_t<Assoc>> &&
    std::default_initializable<detail::remove_cvref_t<Assoc>> &&
    requires(const detail::remove_cvref_t<Assoc>& assoc) {
      { static_cast<bool>(assoc) } noexcept;
      {
        assoc.try_associate()
      } noexcept -> std::same_as<detail::remove_cvref_t<Assoc>>;
    };

/**
 * @brief Concept for tokens that associate and wrap scope-bound senders.
 */
template <class Token>
concept scope_token =
    std::copyable<detail::remove_cvref_t<Token>> &&
    std::is_nothrow_copy_constructible_v<detail::remove_cvref_t<Token>> &&
    std::is_nothrow_move_constructible_v<detail::remove_cvref_t<Token>> &&
    std::is_nothrow_copy_assignable_v<detail::remove_cvref_t<Token>> &&
    std::is_nothrow_move_assignable_v<detail::remove_cvref_t<Token>> &&
    requires(const detail::remove_cvref_t<Token>& token,
             detail::scope_token_test_sender sender) {
      { token.try_associate() } noexcept -> scope_association;
      { token.wrap(std::move(sender)) } noexcept -> bexec::sender;
    };

namespace detail {

template <class Token>
class associate_closure {
 public:
  explicit associate_closure(Token token) noexcept(
      std::is_nothrow_move_constructible_v<Token>)
      : token_(std::move(token)) {}

  template <sender Sender>
  [[nodiscard]] auto operator()(Sender&& sender) const& {
    return make_associated_sender(token_, std::forward<Sender>(sender));
  }

  template <sender Sender>
  [[nodiscard]] auto operator()(Sender&& sender) && {
    return make_associated_sender(std::move(token_),
                                  std::forward<Sender>(sender));
  }

 private:
  Token token_;
};

}  // namespace detail

template <sender Sender, scope_token Token>
[[nodiscard]] auto operator|(Sender&& sender,
                             detail::associate_closure<Token> closure) {
  return std::move(closure)(std::forward<Sender>(sender));
}

/**
 * @brief Pipeable sender adaptor that associates work with a scope token.
 *
 * The returned sender is unspecified. It owns a successful association and
 * completes with set_stopped() without connecting the wrapped child when the
 * token rejects association.
 */
struct associate_t {
  template <sender Sender, scope_token Token>
    requires bexec::sender<
        detail::remove_cvref_t<decltype(detail::wrap_scope_sender(
            std::declval<const Token&>(), std::declval<Sender>()))>>
  [[nodiscard]] auto operator()(Sender&& sender, Token token) const {
    return detail::make_associated_sender(std::move(token),
                                          std::forward<Sender>(sender));
  }

  template <scope_token Token>
  [[nodiscard]] auto operator()(Token token) const {
    return detail::associate_closure<detail::remove_cvref_t<Token>>{
        std::move(token)};
  }
};

inline constexpr associate_t associate{};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_ASSOCIATE_HPP_
