#pragma once

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
    using type = typename concat_type_lists<type_list<As..., Bs...>, Rest...>::type;
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
    using next_seen = std::conditional_t<type_list_contains_v<Head, type_list<Seen...>>,
                                         type_list<Seen...>,
                                         type_list<Seen..., Head>>;
    using type = typename unique_type_list_impl<next_seen, type_list<Tail...>>::type;
};

template <class List>
using unique_type_list_t = typename unique_type_list_impl<type_list<>, List>::type;

template <class List>
struct variant_from_type_list;

template <class... Ts>
struct variant_from_type_list<type_list<Ts...>> {
    using type = std::variant<Ts...>;
};

template <class List>
using variant_from_type_list_t = typename variant_from_type_list<List>::type;

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
using sender_completion_signatures_t = typename sender_completion_signatures<Sender>::type;

template <class Sender>
using sender_error_types_t = typename sender_completion_signatures_t<Sender>::error_types;

template <class Sender>
using sender_value_signatures_t = typename sender_completion_signatures_t<Sender>::value_signatures;

template <class Sender>
inline constexpr bool sender_sends_stopped_v =
    sender_completion_signatures_t<Sender>::sends_stopped;

template <class Fn, class Signature>
struct then_value_signature;

template <class Fn, class... Args>
struct then_value_signature<Fn, value_signature<Args...>> {
    using invoke_result = std::invoke_result_t<Fn&, Args...>;
    using type = std::conditional_t<std::is_void_v<invoke_result>,
                                    value_signature<>,
                                    value_signature<std::decay_t<invoke_result>>>;
};

template <class Fn, class ValueSignatures>
struct then_value_signatures;

template <class Fn, class... Signatures>
struct then_value_signatures<Fn, type_list<Signatures...>> {
    using type = type_list<typename then_value_signature<Fn, Signatures>::type...>;
};

template <class Fn, class Sender>
using then_value_signatures_t =
    typename then_value_signatures<Fn, sender_value_signatures_t<Sender>>::type;

template <class Sender>
using sender_errors_with_exception_t =
    unique_type_list_t<concat_type_lists_t<sender_error_types_t<Sender>,
                                           type_list<std::exception_ptr>>>;

struct empty_callback {
    void operator()() const noexcept {}
};

} // namespace bexec::detail
