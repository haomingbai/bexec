/**
 * @file include/bexec/detail/type_traits.hpp
 * @brief Internal template utilities for sender/adaptor metadata.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides cvref removal, variant membership checks, signature gathering
 * helpers, then completion transformations, and shared type-list helpers for
 * algorithms.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_TYPE_TRAITS_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_TYPE_TRAITS_HPP_

#include <bexec/completion_signatures.hpp>
#include <concepts>
#include <exception>
#include <functional>
#include <type_traits>
#include <variant>

namespace bexec::detail {

template <class>
inline constexpr bool dependent_false = false;

template <class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <class T, class Variant>
struct variant_contains;

template <class T, class... Ts>
struct variant_contains<T, std::variant<Ts...>>
    : std::bool_constant<(std::same_as<T, Ts> || ...)> {};

template <class T, class Variant>
inline constexpr bool variant_contains_v = variant_contains<T, Variant>::value;

template <class Sender>
struct sender_completion_signatures {
  using type = typename remove_cvref_t<Sender>::completion_signatures;
};

template <class Sender>
using sender_completion_signatures_t =
    typename sender_completion_signatures<Sender>::type;

template <class Sender>
using sender_error_types_t =
    gather_signatures_t<set_error_t, sender_completion_signatures_t<Sender>,
                        bexec::single_type, type_list>;

template <class Sender>
inline constexpr bool sender_sends_stopped_v =
    (sender_completion_signatures_t<Sender>::template count_of<
         set_stopped_t>() != 0);

template <class Fn, class Signature>
struct then_signature;

template <class Result>
struct then_value_signature_result {
  using type = set_value_t(std::decay_t<Result>);
};

template <>
struct then_value_signature_result<void> {
  using type = set_value_t();
};

template <class Fn, class... Args>
struct then_signature<Fn, set_value_t(Args...)> {
  using invoke_result = std::invoke_result_t<Fn&, Args...>;
  using signature = typename then_value_signature_result<invoke_result>::type;
  using type = type_list<signature>;
};

template <class Fn, class Error>
struct then_signature<Fn, set_error_t(Error)> {
  using type = type_list<set_error_t(Error)>;
};

template <class Fn>
struct then_signature<Fn, set_stopped_t()> {
  using type = type_list<set_stopped_t()>;
};

template <class Fn, class Completions>
struct then_completion_signatures;

template <class Fn, class... Signatures>
struct then_completion_signatures<Fn, completion_signatures<Signatures...>> {
  using transformed =
      concat_type_lists_t<typename then_signature<Fn, Signatures>::type...>;
  using with_exception = unique_type_list_t<concat_type_lists_t<
      transformed, type_list<set_error_t(std::exception_ptr)>>>;
  using type = completion_signatures_from_type_list_t<with_exception>;
};

template <class Fn, class Sender>
using then_completion_signatures_t = typename then_completion_signatures<
    Fn, sender_completion_signatures_t<Sender>>::type;

template <class Sender>
using sender_errors_with_exception_t =
    unique_type_list_t<concat_type_lists_t<sender_error_types_t<Sender>,
                                           type_list<std::exception_ptr>>>;

template <class ErrorList>
struct set_error_signatures_from_type_list;

template <class... Errors>
struct set_error_signatures_from_type_list<type_list<Errors...>> {
  using type = type_list<set_error_t(Errors)...>;
};

template <class ErrorList>
using set_error_signatures_from_type_list_t =
    typename set_error_signatures_from_type_list<ErrorList>::type;

struct empty_callback {
  void operator()() const noexcept {}
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_TYPE_TRAITS_HPP_
