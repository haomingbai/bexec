/**
 * @file include/bexec/detail/then.hpp
 * @brief Internal receiver adapter for completion-transforming adaptors.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Wraps a downstream receiver, invokes the user callable on the selected
 * completion kind, forwards non-selected completions unchanged, and reports
 * thrown exceptions when exceptions are enabled.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_THEN_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_THEN_HPP_

#include <bexec/detail/config.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <concepts>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace bexec::detail {

template <class Tag, class Receiver, class Fn>
class completion_adaptor_receiver {
 public:
  completion_adaptor_receiver(Receiver receiver, Fn fn)
      : receiver_(std::move(receiver)), fn_(std::move(fn)) {}

  [[nodiscard]] auto get_env() const
      noexcept(noexcept(bexec::get_env(receiver_))) {
    return bexec::get_env(receiver_);
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
    if constexpr (std::same_as<Tag, set_value_t>) {
      complete_selected(std::forward<Args>(args)...);
    } else {
      bexec::set_value(std::move(receiver_), std::forward<Args>(args)...);
    }
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    if constexpr (std::same_as<Tag, set_error_t>) {
      complete_selected(std::forward<Error>(error));
    } else {
      bexec::set_error(std::move(receiver_), std::forward<Error>(error));
    }
  }

  void set_stopped() noexcept {
    if constexpr (std::same_as<Tag, set_stopped_t>) {
      complete_selected();
    } else {
      bexec::set_stopped(std::move(receiver_));
    }
  }

 private:
  template <class... Args>
  void complete_selected(Args&&... args) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      if constexpr (std::is_void_v<std::invoke_result_t<Fn&, Args...>>) {
        std::invoke(fn_, std::forward<Args>(args)...);
        bexec::set_value(std::move(receiver_));
      } else {
        bexec::set_value(std::move(receiver_),
                         std::invoke(fn_, std::forward<Args>(args)...));
      }
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      bexec::set_error(std::move(receiver_), std::current_exception());
    }
#endif
  }

  Receiver receiver_;
  Fn fn_;
};

template <class Receiver, class Fn>
using then_receiver = completion_adaptor_receiver<set_value_t, Receiver, Fn>;

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_THEN_HPP_
