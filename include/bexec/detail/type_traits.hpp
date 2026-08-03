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
 * helpers, then/let completion transformations, and shared type-list helpers
 * for algorithms.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_TYPE_TRAITS_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_TYPE_TRAITS_HPP_

#include <bexec/completion_signatures.hpp>
#include <concepts>
#include <exception>
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace bexec::detail {

template <class>
inline constexpr bool dependent_false = false;

template <class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <class... Ts>
using decayed_tuple = std::tuple<std::decay_t<Ts>...>;

template <bool Include, class... Ts>
struct maybe_type_list {
  using type = type_list<>;
};

template <class... Ts>
struct maybe_type_list<true, Ts...> {
  using type = type_list<Ts...>;
};

template <bool Include, class... Ts>
using maybe_type_list_t = typename maybe_type_list<Include, Ts...>::type;

template <class T, class Variant>
struct variant_contains;

template <class T, class... Ts>
struct variant_contains<T, std::variant<Ts...>>
    : std::bool_constant<(std::same_as<T, Ts> || ...)> {};

template <class T, class Variant>
inline constexpr bool variant_contains_v = variant_contains<T, Variant>::value;

template <class Sender, class... Env>
struct sender_completion_signatures {
  using type = bexec::completion_signatures_of_t<Sender, Env...>;
};

template <class Sender, class... Env>
using sender_completion_signatures_t =
    typename sender_completion_signatures<Sender, Env...>::type;

template <class Sender, class... Env>
using sender_error_types_t =
    gather_signatures_t<set_error_t,
                        sender_completion_signatures_t<Sender, Env...>,
                        bexec::single_type, type_list>;

template <class Sender, class... Env>
inline constexpr bool sender_sends_stopped_v =
    (sender_completion_signatures_t<Sender, Env...>::template count_of<
         set_stopped_t>() != 0);

template <class Tag, class Fn, class Signature>
struct completion_adaptor_signature;

template <class Result>
struct value_signature_result {
  using type = set_value_t(std::decay_t<Result>);
};

template <>
struct value_signature_result<void> {
  using type = set_value_t();
};

template <class Fn, class... Args>
struct completion_adaptor_signature<set_value_t, Fn, set_value_t(Args...)> {
  using invoke_result = std::invoke_result_t<Fn&, Args...>;
  using signature = typename value_signature_result<invoke_result>::type;
  using type = type_list<signature>;
};

template <class Tag, class Fn, class... Args>
  requires(!std::same_as<Tag, set_value_t>)
struct completion_adaptor_signature<Tag, Fn, set_value_t(Args...)> {
  using type = type_list<set_value_t(Args...)>;
};

template <class Fn, class Error>
struct completion_adaptor_signature<set_error_t, Fn, set_error_t(Error)> {
  using invoke_result = std::invoke_result_t<Fn&, Error>;
  using signature = typename value_signature_result<invoke_result>::type;
  using type = type_list<signature>;
};

template <class Tag, class Fn, class Error>
  requires(!std::same_as<Tag, set_error_t>)
struct completion_adaptor_signature<Tag, Fn, set_error_t(Error)> {
  using type = type_list<set_error_t(Error)>;
};

template <class Fn>
struct completion_adaptor_signature<set_stopped_t, Fn, set_stopped_t()> {
  using invoke_result = std::invoke_result_t<Fn&>;
  using signature = typename value_signature_result<invoke_result>::type;
  using type = type_list<signature>;
};

template <class Tag, class Fn>
  requires(!std::same_as<Tag, set_stopped_t>)
struct completion_adaptor_signature<Tag, Fn, set_stopped_t()> {
  using type = type_list<set_stopped_t()>;
};

template <class Tag, class Fn, class Signature>
struct completion_adaptor_invoke_nothrow : std::true_type {};

template <class Fn, class... Args>
struct completion_adaptor_invoke_nothrow<set_value_t, Fn, set_value_t(Args...)>
    : std::is_nothrow_invocable<Fn&, Args...> {};

template <class Fn, class Error>
struct completion_adaptor_invoke_nothrow<set_error_t, Fn, set_error_t(Error)>
    : std::is_nothrow_invocable<Fn&, Error> {};

template <class Fn>
struct completion_adaptor_invoke_nothrow<set_stopped_t, Fn, set_stopped_t()>
    : std::is_nothrow_invocable<Fn&> {};

template <class Tag, class Fn, class Completions>
struct completion_adaptor_completion_signatures;

template <class Tag, class Fn, class... Signatures>
struct completion_adaptor_completion_signatures<
    Tag, Fn, completion_signatures<Signatures...>> {
  using transformed = concat_type_lists_t<
      typename completion_adaptor_signature<Tag, Fn, Signatures>::type...>;
  static constexpr bool all_nothrow =
      (completion_adaptor_invoke_nothrow<Tag, Fn, Signatures>::value && ...);
  using with_exception = unique_type_list_t<concat_type_lists_t<
      transformed,
      maybe_type_list_t<!all_nothrow, set_error_t(std::exception_ptr)>>>;
  using type = completion_signatures_from_type_list_t<with_exception>;
};

template <class Tag, class Fn, class Sender>
using completion_adaptor_completion_signatures_t =
    typename completion_adaptor_completion_signatures<
        Tag, Fn, sender_completion_signatures_t<Sender>>::type;

template <class Fn, class Sender>
using then_completion_signatures_t =
    completion_adaptor_completion_signatures_t<set_value_t, Fn, Sender>;

template <class Tag, class Fn, class Signature>
struct let_signature {
  using type = type_list<Signature>;
};

template <class Fn, class... Args>
struct let_signature<set_value_t, Fn, set_value_t(Args...)> {
  using sender_type = std::invoke_result_t<Fn&, Args...>;
  using type = completion_signatures_to_type_list_t<
      sender_completion_signatures_t<sender_type>>;
};

template <class Fn, class Error>
struct let_signature<set_error_t, Fn, set_error_t(Error)> {
  using sender_type = std::invoke_result_t<Fn&, Error>;
  using type = completion_signatures_to_type_list_t<
      sender_completion_signatures_t<sender_type>>;
};

template <class Fn>
struct let_signature<set_stopped_t, Fn, set_stopped_t()> {
  using sender_type = std::invoke_result_t<Fn&>;
  using type = completion_signatures_to_type_list_t<
      sender_completion_signatures_t<sender_type>>;
};

template <class Tag, class Fn, class Signature>
struct let_invoke_nothrow : std::true_type {};

template <class Fn, class... Args>
struct let_invoke_nothrow<set_value_t, Fn, set_value_t(Args...)>
    : std::is_nothrow_invocable<Fn&, Args...> {};

template <class Fn, class Error>
struct let_invoke_nothrow<set_error_t, Fn, set_error_t(Error)>
    : std::is_nothrow_invocable<Fn&, Error> {};

template <class Fn>
struct let_invoke_nothrow<set_stopped_t, Fn, set_stopped_t()>
    : std::is_nothrow_invocable<Fn&> {};

template <class Tag, class Fn, class Completions>
struct let_completion_signatures;

template <class Tag, class Fn, class... Signatures>
struct let_completion_signatures<Tag, Fn,
                                 completion_signatures<Signatures...>> {
  using transformed =
      concat_type_lists_t<typename let_signature<Tag, Fn, Signatures>::type...>;
  // Only the fn invocation can throw inside let: a nothrow fn means the child
  // sender's completions pass through unchanged and no exception_ptr is added.
  static constexpr bool fn_nothrow =
      (let_invoke_nothrow<Tag, Fn, Signatures>::value && ...);
  using with_exception = unique_type_list_t<concat_type_lists_t<
      transformed,
      maybe_type_list_t<!fn_nothrow, set_error_t(std::exception_ptr)>>>;
  using type = completion_signatures_from_type_list_t<with_exception>;
};

template <class Tag, class Fn, class Sender>
using let_completion_signatures_t = typename let_completion_signatures<
    Tag, Fn, sender_completion_signatures_t<Sender>>::type;

template <class Tag, class Fn, class Env, class Signature>
struct let_signature_for_env {
  using type = type_list<Signature>;
};

template <class Fn, class Env, class... Args>
struct let_signature_for_env<set_value_t, Fn, Env, set_value_t(Args...)> {
  using sender_type = std::invoke_result_t<Fn&, Args...>;
  using type = completion_signatures_to_type_list_t<
      sender_completion_signatures_t<sender_type, Env>>;
};

template <class Fn, class Env, class Error>
struct let_signature_for_env<set_error_t, Fn, Env, set_error_t(Error)> {
  using sender_type = std::invoke_result_t<Fn&, Error>;
  using type = completion_signatures_to_type_list_t<
      sender_completion_signatures_t<sender_type, Env>>;
};

template <class Fn, class Env>
struct let_signature_for_env<set_stopped_t, Fn, Env, set_stopped_t()> {
  using sender_type = std::invoke_result_t<Fn&>;
  using type = completion_signatures_to_type_list_t<
      sender_completion_signatures_t<sender_type, Env>>;
};

template <class Tag, class Fn, class Env, class Completions>
struct let_completion_signatures_for_env;

template <class Tag, class Fn, class Env, class... Signatures>
struct let_completion_signatures_for_env<Tag, Fn, Env,
                                         completion_signatures<Signatures...>> {
  using transformed = concat_type_lists_t<
      typename let_signature_for_env<Tag, Fn, Env, Signatures>::type...>;
  static constexpr bool fn_nothrow =
      (let_invoke_nothrow<Tag, Fn, Signatures>::value && ...);
  using with_exception = unique_type_list_t<concat_type_lists_t<
      transformed,
      maybe_type_list_t<!fn_nothrow, set_error_t(std::exception_ptr)>>>;
  using type = completion_signatures_from_type_list_t<with_exception>;
};

template <class Tag, class Fn, class Sender, class Env>
using let_completion_signatures_for_env_t =
    typename let_completion_signatures_for_env<
        Tag, Fn, Env, sender_completion_signatures_t<Sender, Env>>::type;

template <class Sender, class... Env>
using sender_errors_with_exception_t =
    unique_type_list_t<concat_type_lists_t<sender_error_types_t<Sender, Env...>,
                                           type_list<std::exception_ptr>>>;

template <class Signature>
struct decayed_completion_signature;

template <class Tag, class... Args>
struct decayed_completion_signature<Tag(Args...)> {
  using type = Tag(std::decay_t<Args>...);
};

template <class Signature>
using decayed_completion_signature_t =
    typename decayed_completion_signature<Signature>::type;

template <class Signature>
struct completion_result_tuple;

template <class Tag, class... Args>
struct completion_result_tuple<Tag(Args...)> {
  using type = std::tuple<Tag, std::decay_t<Args>...>;
};

template <class Signature>
using completion_result_tuple_t =
    typename completion_result_tuple<Signature>::type;

template <class Signature>
struct value_tuple_for_signature {
  using type = type_list<>;
};

template <class... Args>
struct value_tuple_for_signature<set_value_t(Args...)> {
  using type = type_list<decayed_tuple<Args...>>;
};

template <class Completions>
struct value_tuple_list;

template <class... Signatures>
struct value_tuple_list<completion_signatures<Signatures...>> {
  using type = concat_type_lists_t<
      typename value_tuple_for_signature<Signatures>::type...>;
};

template <class Completions>
using value_tuple_list_t = typename value_tuple_list<Completions>::type;

template <class Sender, class... Env>
using sender_value_tuple_list_t =
    value_tuple_list_t<sender_completion_signatures_t<Sender, Env...>>;

template <class Sender, class... Env>
using sender_unique_value_tuple_list_t =
    unique_type_list_t<sender_value_tuple_list_t<Sender, Env...>>;

template <class List>
struct type_list_size;

template <class... Ts>
struct type_list_size<type_list<Ts...>>
    : std::integral_constant<std::size_t, sizeof...(Ts)> {};

template <class List>
inline constexpr std::size_t type_list_size_v = type_list_size<List>::value;

template <class List>
struct first_type_in_type_list;

template <class T, class... Rest>
struct first_type_in_type_list<type_list<T, Rest...>> {
  using type = T;
};

template <class List>
using first_type_in_type_list_t = typename first_type_in_type_list<List>::type;

template <class Sender, class... Env>
inline constexpr std::size_t sender_value_completion_count_v =
    type_list_size_v<sender_value_tuple_list_t<Sender, Env...>>;

template <class Sender, class... Env>
inline constexpr bool sender_has_single_value_completion_v =
    (sender_value_completion_count_v<Sender, Env...> == 1U);

template <class Sender, class... Env>
using sender_single_value_tuple_t =
    first_type_in_type_list_t<sender_value_tuple_list_t<Sender, Env...>>;

template <class Sender, class... Env>
using sender_value_slot_t =
    std::optional<sender_single_value_tuple_t<Sender, Env...>>;

template <class Tuple>
struct set_value_signature_from_tuple;

template <class... Args>
struct set_value_signature_from_tuple<std::tuple<Args...>> {
  using type = set_value_t(Args...);
};

template <class Tuple>
using set_value_signature_from_tuple_t =
    typename set_value_signature_from_tuple<Tuple>::type;

template <class... Tuples>
struct tuple_cat_type;

template <class... Tuples>
struct tuple_cat_type {
  using type = decltype(std::tuple_cat(std::declval<Tuples>()...));
};

template <class... Tuples>
using tuple_cat_type_t = typename tuple_cat_type<Tuples...>::type;

template <class List>
struct set_value_signatures_from_tuple_list;

template <class... Tuples>
struct set_value_signatures_from_tuple_list<type_list<Tuples...>> {
  using type = type_list<set_value_signature_from_tuple_t<Tuples>...>;
};

template <class List>
using set_value_signatures_from_tuple_list_t =
    typename set_value_signatures_from_tuple_list<List>::type;

template <bool AllSingle, class... Senders>
struct when_all_value_signature_list_impl {
  using type = type_list<>;
};

template <class... Senders>
struct when_all_value_signature_list_impl<true, Senders...> {
  using type = type_list<set_value_signature_from_tuple_t<
      tuple_cat_type_t<sender_single_value_tuple_t<Senders>...>>>;
};

template <class... Senders>
struct when_all_value_signature_list
    : when_all_value_signature_list_impl<
          (sender_has_single_value_completion_v<Senders> && ...), Senders...> {
};

template <class... Senders>
using when_all_value_signature_list_t =
    typename when_all_value_signature_list<Senders...>::type;

template <bool AllSingle, class Env, class... Senders>
struct when_all_value_signature_list_for_env_impl {
  using type = type_list<>;
};

template <class Env, class... Senders>
struct when_all_value_signature_list_for_env_impl<true, Env, Senders...> {
  using type = type_list<set_value_signature_from_tuple_t<
      tuple_cat_type_t<sender_single_value_tuple_t<Senders, Env>...>>>;
};

template <class Env, class... Senders>
struct when_all_value_signature_list_for_env
    : when_all_value_signature_list_for_env_impl<
          (sender_has_single_value_completion_v<Senders, Env> && ...), Env,
          Senders...> {};

template <class Env, class... Senders>
using when_all_value_signature_list_for_env_t =
    typename when_all_value_signature_list_for_env<Env, Senders...>::type;

template <class ErrorList>
struct set_error_signatures_from_type_list;

template <class... Errors>
struct set_error_signatures_from_type_list<type_list<Errors...>> {
  using type = type_list<set_error_t(Errors)...>;
};

template <class ErrorList>
using set_error_signatures_from_type_list_t =
    typename set_error_signatures_from_type_list<ErrorList>::type;

// ---------------------------------------------------------------------------
// Compile-time noexcept extraction helpers.
//
// These mirror the runtime `if constexpr` fast paths: when every value/error
// completion can be stored without throwing, and every user callable on the
// path is nothrow-invocable, an adaptor's completion_signatures need not
// declare set_error_t(std::exception_ptr). Non-matching signatures are treated
// as nothrow so an empty category (e.g. no error completions) folds to true.
// ---------------------------------------------------------------------------

// Whether the arguments of one value completion nothrow-construct the stored
// decayed tuple (the runtime store_value fast-path condition).
template <class Signature>
struct value_signature_nothrow_store : std::true_type {};

template <class... Args>
struct value_signature_nothrow_store<set_value_t(Args...)>
    : std::is_nothrow_constructible<decayed_tuple<Args...>, Args...> {};

// Whether the error type of one error completion nothrow-constructs its stored
// decayed form (the runtime store_error fast-path condition).
template <class Signature>
struct error_signature_nothrow_store : std::true_type {};

template <class Error>
struct error_signature_nothrow_store<set_error_t(Error)>
    : std::is_nothrow_constructible<std::decay_t<Error>, Error> {};

template <class Completions>
struct all_values_nothrow_store;

template <class... Signatures>
struct all_values_nothrow_store<completion_signatures<Signatures...>>
    : std::bool_constant<(value_signature_nothrow_store<Signatures>::value &&
                          ...)> {};

template <class Completions>
inline constexpr bool all_values_nothrow_store_v =
    all_values_nothrow_store<Completions>::value;

template <class Completions>
struct all_errors_nothrow_store;

template <class... Signatures>
struct all_errors_nothrow_store<completion_signatures<Signatures...>>
    : std::bool_constant<(error_signature_nothrow_store<Signatures>::value &&
                          ...)> {};

template <class Completions>
inline constexpr bool all_errors_nothrow_store_v =
    all_errors_nothrow_store<Completions>::value;

// Whether every argument of every completion signature nothrow-constructs its
// stored decayed form (the spawn_future set_complete fast-path condition).
template <class Signature>
struct completion_args_nothrow_store : std::true_type {};

template <class... Args>
struct completion_args_nothrow_store<set_value_t(Args...)>
    : std::bool_constant<(
          std::is_nothrow_constructible_v<std::decay_t<Args>, Args> && ...)> {};

template <class Error>
struct completion_args_nothrow_store<set_error_t(Error)>
    : std::bool_constant<
          std::is_nothrow_constructible_v<std::decay_t<Error>, Error>> {};

template <class Completions>
struct all_completion_args_nothrow_store;

template <class... Signatures>
struct all_completion_args_nothrow_store<completion_signatures<Signatures...>>
    : std::bool_constant<(completion_args_nothrow_store<Signatures>::value &&
                          ...)> {};

template <class Completions>
inline constexpr bool all_completion_args_nothrow_store_v =
    all_completion_args_nothrow_store<Completions>::value;

template <class Tuple>
struct value_completion {
  explicit value_completion(Tuple tuple) : values(std::move(tuple)) {}
  Tuple values;
};

template <class Error>
struct error_completion {
  explicit error_completion(Error error_value)
      : error(std::move(error_value)) {}
  Error error;
};

struct stopped_completion {};

template <class Signature>
struct completion_variant_alternative {
  using type = type_list<>;
};

template <class... Args>
struct completion_variant_alternative<set_value_t(Args...)> {
  using type = type_list<value_completion<decayed_tuple<Args...>>>;
};

template <class Error>
struct completion_variant_alternative<set_error_t(Error)> {
  using type = type_list<error_completion<std::decay_t<Error>>>;
};

template <>
struct completion_variant_alternative<set_stopped_t()> {
  using type = type_list<stopped_completion>;
};

template <class Completions>
struct completion_variant_type_list;

template <class... Signatures>
struct completion_variant_type_list<completion_signatures<Signatures...>> {
  using type = unique_type_list_t<concat_type_lists_t<
      typename completion_variant_alternative<Signatures>::type...>>;
};

template <class Completions>
using completion_variant_t = variant_from_type_list_t<
    typename completion_variant_type_list<Completions>::type>;

template <class List>
struct value_variant_from_tuple_list;

template <class... Tuples>
struct value_variant_from_tuple_list<type_list<Tuples...>> {
  using type = variant_from_type_list_t<type_list<Tuples...>>;
};

template <class List>
using value_variant_from_tuple_list_t =
    typename value_variant_from_tuple_list<List>::type;

template <class Sender>
using into_variant_value_variant_t =
    value_variant_from_tuple_list_t<sender_unique_value_tuple_list_t<Sender>>;

template <class Sender>
using into_variant_value_signature_list_t =
    type_list<set_value_t(into_variant_value_variant_t<Sender>)>;

template <class Variant, class TupleList>
struct variant_all_tuples_nothrow;

template <class Variant, class... Tuples>
struct variant_all_tuples_nothrow<Variant, type_list<Tuples...>>
    : std::bool_constant<(std::is_nothrow_constructible_v<Variant, Tuples> &&
                          ...)> {};

template <class Sender>
struct into_variant_completion_signatures {
  using value_variant = into_variant_value_variant_t<Sender>;
  using values = into_variant_value_signature_list_t<Sender>;
  // The runtime fast path stores the tuple, then wraps it into the variant via
  // std::variant's converting constructor. Mirror both conditions.
  static constexpr bool values_nothrow =
      all_values_nothrow_store_v<sender_completion_signatures_t<Sender>> &&
      variant_all_tuples_nothrow<
          value_variant, sender_unique_value_tuple_list_t<Sender>>::value;
  using errors = set_error_signatures_from_type_list_t<
      unique_type_list_t<concat_type_lists_t<
          sender_error_types_t<Sender>,
          maybe_type_list_t<!values_nothrow, std::exception_ptr>>>>;
  using stopped = std::conditional_t<sender_sends_stopped_v<Sender>,
                                     type_list<set_stopped_t()>, type_list<>>;
  using signatures =
      unique_type_list_t<concat_type_lists_t<values, errors, stopped>>;
  using type = completion_signatures_from_type_list_t<signatures>;
};

template <class Sender>
using into_variant_completion_signatures_t =
    typename into_variant_completion_signatures<Sender>::type;

struct empty_callback {
  void operator()() const noexcept {}
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_TYPE_TRAITS_HPP_
