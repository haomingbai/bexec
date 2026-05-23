/**
 * @file include/bexec/completion_signatures.hpp
 * @brief Completion-signature metadata and type-list utilities.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines P2300-style completion signature validation, completion_signatures
 * packs, type-list helpers, and signature introspection utilities used by
 * senders and adaptors.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_COMPLETION_SIGNATURES_HPP_
#define BEXEC_INCLUDE_BEXEC_COMPLETION_SIGNATURES_HPP_

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <variant>

namespace bexec {

struct set_value_t;
struct set_error_t;
struct set_stopped_t;

template <class... Ts>
struct type_list {};

namespace detail {

template <class Signature>
struct is_completion_signature : std::false_type {};

template <class... Args>
struct is_completion_signature<set_value_t(Args...)>
    : std::bool_constant<(
          (std::is_object_v<Args> || std::is_reference_v<Args>) && ...)> {};

template <class Error>
struct is_completion_signature<set_error_t(Error)>
    : std::bool_constant<std::is_object_v<Error> ||
                         std::is_reference_v<Error>> {};

template <>
struct is_completion_signature<set_stopped_t()> : std::true_type {};

template <class Signature>
inline constexpr bool is_completion_signature_v =
    is_completion_signature<Signature>::value;

template <class Tag, class Signature>
struct completion_signature_matches : std::false_type {};

template <class Tag, class... Args>
struct completion_signature_matches<Tag, Tag(Args...)> : std::true_type {};

template <class Tag, class Signature>
inline constexpr bool completion_signature_matches_v =
    completion_signature_matches<Tag, Signature>::value;

}  // namespace detail

/**
 * @brief P2300-style completion signature function type.
 */
template <class Signature>
concept completion_signature = detail::is_completion_signature_v<Signature>;

/**
 * @brief Encodes the set of completion operations a sender may perform.
 */
template <completion_signature... Signatures>
struct completion_signatures {
  template <class Tag>
  [[nodiscard]] static consteval std::size_t count_of() noexcept {
    return (std::size_t{0} + ... +
            (detail::completion_signature_matches_v<Tag, Signatures>
                 ? std::size_t{1}
                 : std::size_t{0}));
  }

  template <class Fn>
  static constexpr void for_each(Fn&& fn) {
    (fn(static_cast<Signatures*>(nullptr)), ...);
  }
};

template <class>
struct is_completion_signatures : std::false_type {};

template <class... Signatures>
struct is_completion_signatures<completion_signatures<Signatures...>>
    : std::true_type {};

template <class Completions>
concept valid_completion_signatures =
    is_completion_signatures<Completions>::value;

namespace detail {

template <class...>
inline constexpr bool dependent_false_v = false;

template <class Sender, class... Env>
concept has_member_get_completion_signatures = requires {
  std::remove_cvref_t<Sender>::template get_completion_signatures<Sender,
                                                                  Env...>();
};

template <class Sender>
concept has_nested_completion_signatures =
    requires { typename std::remove_cvref_t<Sender>::completion_signatures; };

template <class... Lists>
struct concat_type_lists;

template <>
struct concat_type_lists<> {
  using type = type_list<>;
};

template <class... Ts>
struct concat_type_lists<type_list<Ts...>> {
  using type = type_list<Ts...>;
};

template <class... As, class... Bs, class... Rest>
struct concat_type_lists<type_list<As...>, type_list<Bs...>, Rest...> {
  using type =
      typename concat_type_lists<type_list<As..., Bs...>, Rest...>::type;
};

template <class... Lists>
using concat_type_lists_t = typename concat_type_lists<Lists...>::type;

template <class T, class List>
struct type_list_contains;

template <class T>
struct type_list_contains<T, type_list<>> : std::false_type {};

template <class T, class Head, class... Tail>
struct type_list_contains<T, type_list<Head, Tail...>>
    : std::conditional_t<std::same_as<T, Head>, std::true_type,
                         type_list_contains<T, type_list<Tail...>>> {};

template <class T, class List>
inline constexpr bool type_list_contains_v = type_list_contains<T, List>::value;

template <class Seen, class Input>
struct unique_type_list_impl;

template <class... Seen>
struct unique_type_list_impl<type_list<Seen...>, type_list<>> {
  using type = type_list<Seen...>;
};

template <class... Seen, class Head, class... Tail>
struct unique_type_list_impl<type_list<Seen...>, type_list<Head, Tail...>> {
  using next_seen =
      std::conditional_t<type_list_contains_v<Head, type_list<Seen...>>,
                         type_list<Seen...>, type_list<Seen..., Head>>;
  using type =
      typename unique_type_list_impl<next_seen, type_list<Tail...>>::type;
};

template <class List>
using unique_type_list_t =
    typename unique_type_list_impl<type_list<>, List>::type;

template <class List>
struct completion_signatures_from_type_list;

template <class... Signatures>
struct completion_signatures_from_type_list<type_list<Signatures...>> {
  using type = completion_signatures<Signatures...>;
};

template <class List>
using completion_signatures_from_type_list_t =
    typename completion_signatures_from_type_list<List>::type;

template <class Completions>
struct completion_signatures_to_type_list;

template <class... Signatures>
struct completion_signatures_to_type_list<
    completion_signatures<Signatures...>> {
  using type = type_list<Signatures...>;
};

template <class Completions>
using completion_signatures_to_type_list_t =
    typename completion_signatures_to_type_list<Completions>::type;

template <class Signature, class Tag, template <class...> class Tuple>
struct gather_one_signature {
  using type = type_list<>;
};

template <class Tag, class... Args, template <class...> class Tuple>
struct gather_one_signature<Tag(Args...), Tag, Tuple> {
  using type = type_list<Tuple<Args...>>;
};

template <class List, template <class...> class Variant>
struct apply_variant_to_type_list;

template <class... Ts, template <class...> class Variant>
struct apply_variant_to_type_list<type_list<Ts...>, Variant> {
  using type = Variant<Ts...>;
};

template <class Tag, valid_completion_signatures Completions,
          template <class...> class Tuple, template <class...> class Variant>
struct gather_signatures;

template <class Tag, class... Signatures, template <class...> class Tuple,
          template <class...> class Variant>
struct gather_signatures<Tag, completion_signatures<Signatures...>, Tuple,
                         Variant> {
  using gathered = concat_type_lists_t<
      typename gather_one_signature<Signatures, Tag, Tuple>::type...>;
  using type = typename apply_variant_to_type_list<gathered, Variant>::type;
};

template <class Tag, valid_completion_signatures Completions,
          template <class...> class Tuple, template <class...> class Variant>
using gather_signatures_t =
    typename gather_signatures<Tag, Completions, Tuple, Variant>::type;

template <class List>
struct variant_from_type_list;

template <class... Ts>
struct variant_from_type_list<type_list<Ts...>> {
  using type = std::variant<Ts...>;
};

template <class List>
using variant_from_type_list_t = typename variant_from_type_list<List>::type;

template <class... Ts>
struct variant_or_empty {
  using type = std::variant<Ts...>;
};

template <>
struct variant_or_empty<> {
  using type = type_list<>;
};

template <class... Ts>
struct single_type;

template <class T>
struct single_type<T> {
  using type = T;
};

}  // namespace detail

template <class... Ts>
using variant_or_empty = typename detail::variant_or_empty<Ts...>::type;

template <class... Ts>
using single_type = typename detail::single_type<Ts...>::type;

/**
 * @brief Returns the completion signatures declared by a sender.
 *
 * The standard spelling is environment-aware:
 * get_completion_signatures<Sndr, Env>().
 *
 * bexec keeps the old nested completion_signatures fallback so existing
 * senders remain source-compatible, and also accepts a standard-style static
 * template member get_completion_signatures<Sndr, Env...>() for senders whose
 * completion set depends on the receiver environment.
 */
template <class Sender, class... Env>
  requires(sizeof...(Env) <= 1 &&
           (detail::has_member_get_completion_signatures<Sender, Env...> ||
            detail::has_nested_completion_signatures<Sender>))
[[nodiscard]] consteval auto get_completion_signatures() {
  using sender_type = std::remove_cvref_t<Sender>;
  if constexpr (detail::has_member_get_completion_signatures<Sender, Env...>) {
    return sender_type::template get_completion_signatures<Sender, Env...>();
  } else {
    static_assert(detail::has_nested_completion_signatures<Sender>);
    return typename sender_type::completion_signatures{};
  }
}

template <class Sender, class... Env>
  requires(sizeof...(Env) <= 1)
using completion_signatures_of_t =
    decltype(bexec::get_completion_signatures<Sender, Env...>());

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_COMPLETION_SIGNATURES_HPP_
