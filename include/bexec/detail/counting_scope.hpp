/**
 * @file include/bexec/detail/counting_scope.hpp
 * @brief Internal helpers for counting scopes.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-28
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines implementation-only stop-token wrappers and join helpers used by
 * public simple_counting_scope and counting_scope. Associated sender and spawn
 * machinery lives in detail/associate.hpp.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_COUNTING_SCOPE_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_COUNTING_SCOPE_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/detail/associate.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/receiver.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <bexec/stop_token.hpp>
#include <concepts>
#include <type_traits>
#include <utility>

namespace bexec::detail {

template <class Scheduler>
using scope_schedule_sender_for_t =
    remove_cvref_t<decltype(bexec::schedule(std::declval<Scheduler&>()))>;

template <class Receiver>
using scope_receiver_scheduler_t = remove_cvref_t<decltype(bexec::get_scheduler(
    bexec::get_env(std::declval<Receiver&>())))>;

struct scope_join_waiter {
  scope_join_waiter* next{nullptr};
  virtual void complete_deferred() noexcept = 0;

 protected:
  ~scope_join_waiter() = default;
};

template <class Receiver>
class scope_stop_receiver {
 public:
  scope_stop_receiver(Receiver receiver, inplace_stop_token scope_token)
      : receiver_(std::move(receiver)), scope_token_(std::move(scope_token)) {}

  [[nodiscard]] auto get_env() const
      noexcept(noexcept(bexec::get_env(receiver_))) {
    return env_with_scope_stop_token{scope_token_, bexec::get_env(receiver_)};
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
    bexec::set_value(std::move(receiver_), std::forward<Args>(args)...);
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    bexec::set_error(std::move(receiver_), std::forward<Error>(error));
  }

  void set_stopped() noexcept { bexec::set_stopped(std::move(receiver_)); }

 private:
  Receiver receiver_;
  inplace_stop_token scope_token_;
};

template <class Sender>
class scope_stop_sender {
 public:
  using completion_signatures = bexec::completion_signatures_of_t<Sender>;

  template <class Self, class Env>
  [[nodiscard]] static consteval auto get_completion_signatures() {
    return bexec::completion_signatures_of_t<Sender, Env>{};
  }

  scope_stop_sender(Sender sender, inplace_stop_token scope_token)
      : sender_(std::move(sender)), scope_token_(std::move(scope_token)) {}

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return bexec::connect(
        std::move(sender_),
        scope_stop_receiver<Receiver>{std::move(receiver), scope_token_});
  }

  template <class Receiver>
    requires std::copy_constructible<Sender>
  auto connect(Receiver receiver) const& {
    return bexec::connect(sender_, scope_stop_receiver<Receiver>{
                                       std::move(receiver), scope_token_});
  }

 private:
  Sender sender_;
  inplace_stop_token scope_token_;
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_COUNTING_SCOPE_HPP_
