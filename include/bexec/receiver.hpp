#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_RECEIVER_HPP_
#define BEXEC_INCLUDE_BEXEC_RECEIVER_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/env.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace bexec {

/**
 * @brief Delivers a value completion by calling receiver.set_value(args...).
 */
struct set_value_t {
    template <class Receiver, class... Args>
        requires(!std::is_lvalue_reference_v<Receiver> &&
                 !std::is_const_v<std::remove_reference_t<Receiver>> &&
                 requires(Receiver&& receiver, Args&&... args) {
                     { std::move(receiver).set_value(std::forward<Args>(args)...) } noexcept
                         -> std::same_as<void>;
                 })
    constexpr void operator()(Receiver&& receiver, Args&&... args) const noexcept {
        std::move(receiver).set_value(std::forward<Args>(args)...);
    }
};

inline constexpr set_value_t set_value{};

/**
 * @brief Delivers an error completion by calling receiver.set_error(error).
 */
struct set_error_t {
    template <class Receiver, class Error>
        requires(!std::is_lvalue_reference_v<Receiver> &&
                 !std::is_const_v<std::remove_reference_t<Receiver>> &&
                 requires(Receiver&& receiver, Error&& error) {
                     { std::move(receiver).set_error(std::forward<Error>(error)) } noexcept
                         -> std::same_as<void>;
                 })
    constexpr void operator()(Receiver&& receiver, Error&& error) const noexcept {
        std::move(receiver).set_error(std::forward<Error>(error));
    }
};

inline constexpr set_error_t set_error{};

/**
 * @brief Delivers stopped completion by calling receiver.set_stopped().
 */
struct set_stopped_t {
    template <class Receiver>
        requires(!std::is_lvalue_reference_v<Receiver> &&
                 !std::is_const_v<std::remove_reference_t<Receiver>> &&
                 requires(Receiver&& receiver) {
                     { std::move(receiver).set_stopped() } noexcept -> std::same_as<void>;
                 })
    constexpr void operator()(Receiver&& receiver) const noexcept {
        std::move(receiver).set_stopped();
    }
};

inline constexpr set_stopped_t set_stopped{};

/**
 * @brief Receiver environment CPO.
 *
 * get_env(receiver) calls a const receiver.get_env() when available, otherwise
 * returns empty_env.
 */
struct get_env_t {
    template <class Receiver>
    constexpr auto operator()(Receiver&& receiver) const noexcept {
        if constexpr (requires { std::as_const(receiver).get_env(); }) {
            return std::as_const(receiver).get_env();
        } else {
            return empty_env{};
        }
    }
};

inline constexpr get_env_t get_env{};

/**
 * @brief Basic receiver concept for bexec completion functions.
 */
template <class Receiver>
concept receiver =
    std::move_constructible<std::remove_cvref_t<Receiver>>;

namespace detail {

template <class Receiver, class Signature>
struct valid_completion_for : std::false_type {};

template <class Receiver, class... Args>
struct valid_completion_for<Receiver, set_value_t(Args...)>
    : std::bool_constant<requires(std::remove_cvref_t<Receiver> receiver) {
          bexec::set_value(std::move(receiver), std::declval<Args>()...);
      }> {};

template <class Receiver, class Error>
struct valid_completion_for<Receiver, set_error_t(Error)>
    : std::bool_constant<requires(std::remove_cvref_t<Receiver> receiver) {
          bexec::set_error(std::move(receiver), std::declval<Error>());
      }> {};

template <class Receiver>
struct valid_completion_for<Receiver, set_stopped_t()>
    : std::bool_constant<requires(std::remove_cvref_t<Receiver> receiver) {
          bexec::set_stopped(std::move(receiver));
      }> {};

template <class Receiver, class Completions>
struct receiver_accepts_all;

template <class Receiver, class... Signatures>
struct receiver_accepts_all<Receiver, completion_signatures<Signatures...>>
    : std::bool_constant<(valid_completion_for<Receiver, Signatures>::value && ...)> {};

} // namespace detail

/**
 * @brief Concept for receivers that accept the supplied completion signatures.
 */
template <class Receiver, class Completions>
concept receiver_of =
    valid_completion_signatures<Completions> &&
    receiver<Receiver> &&
    detail::receiver_accepts_all<Receiver, Completions>::value;

} // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_RECEIVER_HPP_
