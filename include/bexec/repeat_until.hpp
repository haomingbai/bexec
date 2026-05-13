/**
 * @file include/bexec/repeat_until.hpp
 * @brief Public repeat_until sender algorithm.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines the sender facade and callable object that repeat freshly-created
 * child senders until a predicate succeeds while discarding child values.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_REPEAT_UNTIL_HPP_
#define BEXEC_INCLUDE_BEXEC_REPEAT_UNTIL_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/detail/repeat_until.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/receiver.hpp>
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace bexec {

/**
 * @brief Sender that repeats a sender-producing callable until a predicate
 * succeeds.
 *
 * Child sender values are discarded. This design intentionally uses a factory
 * callable so every iteration receives a fresh sender and operation state.
 */
template <class Factory, class Predicate>
class repeat_until_sender {
 public:
  using sender_type = std::invoke_result_t<Factory&>;
  using completion_signatures = detail::completion_signatures_from_type_list_t<
      detail::concat_type_lists_t<
          type_list<set_value_t(), set_stopped_t()>,
          detail::set_error_signatures_from_type_list_t<
              detail::sender_errors_with_exception_t<sender_type>>>>;

  repeat_until_sender(Factory factory, Predicate predicate)
      : factory_(std::move(factory)), predicate_(std::move(predicate)) {}

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return detail::repeat_until_operation<Factory, Predicate, Receiver>{
        std::move(factory_), std::move(predicate_), std::move(receiver)};
  }

  template <class Receiver>
    requires std::copy_constructible<Factory> &&
             std::copy_constructible<Predicate>
  auto connect(Receiver receiver) const& {
    return detail::repeat_until_operation<Factory, Predicate, Receiver>{
        factory_, predicate_, std::move(receiver)};
  }

 private:
  Factory factory_;
  Predicate predicate_;
};

/**
 * @brief Function object that creates repeat_until senders.
 */
struct repeat_until_t {
  template <class Factory, class Predicate>
  [[nodiscard]] auto operator()(Factory&& factory,
                                Predicate&& predicate) const {
    return repeat_until_sender<std::decay_t<Factory>, std::decay_t<Predicate>>{
        std::forward<Factory>(factory), std::forward<Predicate>(predicate)};
  }
};

inline constexpr repeat_until_t repeat_until{};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_REPEAT_UNTIL_HPP_
