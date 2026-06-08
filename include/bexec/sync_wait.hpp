/**
 * @file include/bexec/sync_wait.hpp
 * @brief Blocking this_thread sync_wait algorithms.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_SYNC_WAIT_HPP_
#define BEXEC_INCLUDE_BEXEC_SYNC_WAIT_HPP_

#include <bexec/detail/config.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/into_variant.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sender.hpp>
#include <bexec/stop_token.hpp>
#include <cassert>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace bexec::this_thread {

namespace detail {

template <class Sender,
          std::size_t Count = bexec::detail::sender_value_completion_count_v<
              bexec::detail::remove_cvref_t<Sender>>>
struct sync_wait_value_tuple {
  static_assert(Count <= 1U,
                "sync_wait requires at most one value completion; use "
                "sync_wait_with_variant for multiple value alternatives");
  using type = std::tuple<>;
};

template <class Sender>
struct sync_wait_value_tuple<Sender, 1U> {
  using type = bexec::detail::sender_single_value_tuple_t<
      bexec::detail::remove_cvref_t<Sender>>;
};

template <class Sender>
using sync_wait_value_tuple_t = typename sync_wait_value_tuple<Sender>::type;

template <class Sender>
using sync_wait_error_variant_t = bexec::detail::variant_from_type_list_t<
    bexec::detail::sender_errors_with_exception_t<
        bexec::detail::remove_cvref_t<Sender>>>;

class sync_wait_env {
 public:
  sync_wait_env(bexec::run_loop::scheduler scheduler,
                bexec::inplace_stop_token token)
      : scheduler_(scheduler), token_(token) {}

  [[nodiscard]] bexec::run_loop::scheduler query(
      bexec::get_scheduler_t) const noexcept {
    return scheduler_;
  }

  [[nodiscard]] bexec::run_loop::scheduler query(
      bexec::get_delegation_scheduler_t) const noexcept {
    return scheduler_;
  }

  [[nodiscard]] bexec::inplace_stop_token query(
      bexec::get_stop_token_t) const noexcept {
    return token_;
  }

 private:
  bexec::run_loop::scheduler scheduler_;
  bexec::inplace_stop_token token_;
};

template <class ResultTuple, class ErrorVariant>
struct sync_wait_state {
  explicit sync_wait_state(bexec::run_loop& loop) : loop(&loop) {}

  bexec::run_loop* loop;
  bexec::inplace_stop_source stop_source;
  std::optional<ResultTuple> value;
  std::optional<ErrorVariant> error;
  bool stopped{false};
};

template <class ResultTuple, class ErrorVariant>
class sync_wait_receiver {
 public:
  explicit sync_wait_receiver(sync_wait_state<ResultTuple, ErrorVariant>& state)
      : state_(&state) {}

  [[nodiscard]] sync_wait_env get_env() const noexcept {
    return sync_wait_env{state_->loop->get_scheduler(),
                         state_->stop_source.get_token()};
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      state_->value.emplace(std::forward<Args>(args)...);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      state_->error.emplace(std::in_place_type<std::exception_ptr>,
                            std::current_exception());
    }
#endif
    state_->loop->finish();
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    using error_type = std::decay_t<Error>;
    state_->error.emplace(std::in_place_type<error_type>,
                          std::forward<Error>(error));
    state_->loop->finish();
  }

  void set_stopped() noexcept {
    state_->stopped = true;
    state_->loop->finish();
  }

 private:
  sync_wait_state<ResultTuple, ErrorVariant>* state_;
};

template <class ErrorVariant>
[[noreturn]] void throw_error(ErrorVariant error) {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
  std::visit(
      [](auto& value) -> void {
        using error_type = std::decay_t<decltype(value)>;
        if constexpr (std::same_as<error_type, std::exception_ptr>) {
          std::rethrow_exception(value);
        } else {
          throw std::move(value);
        }
      },
      error);
#else
  (void)error;
#endif
  assert(false);
  BEXEC_DETAIL_UNREACHABLE();
}

}  // namespace detail

/**
 * @brief Starts a sender, runs a local run_loop, and returns its value tuple.
 *
 * Stopped completion returns std::nullopt. Error completion is thrown; an
 * std::exception_ptr error is rethrown.
 */
template <bexec::sender Sender>
[[nodiscard]] auto sync_wait(Sender&& sender)
    -> std::optional<detail::sync_wait_value_tuple_t<Sender>> {
  using result_tuple = detail::sync_wait_value_tuple_t<Sender>;
  using error_variant = detail::sync_wait_error_variant_t<Sender>;
  using receiver_type = detail::sync_wait_receiver<result_tuple, error_variant>;
  using state_type = detail::sync_wait_state<result_tuple, error_variant>;

  bexec::run_loop loop;
  state_type state{loop};
  auto operation =
      bexec::connect(std::forward<Sender>(sender), receiver_type{state});

  bexec::start(operation);
  loop.run();

  if (state.error) {
    detail::throw_error(std::move(*state.error));
  }
  if (state.stopped) {
    return std::nullopt;
  }
  return std::move(state.value);
}

/**
 * @brief Variant-returning sync_wait for senders with multiple value
 * alternatives.
 */
template <bexec::sender Sender>
  requires(bexec::detail::sender_value_completion_count_v<
               bexec::detail::remove_cvref_t<Sender>> != 0U)
[[nodiscard]] auto sync_wait_with_variant(Sender&& sender)
    -> std::optional<bexec::detail::into_variant_value_variant_t<
        bexec::detail::remove_cvref_t<Sender>>> {
  using variant_type = bexec::detail::into_variant_value_variant_t<
      bexec::detail::remove_cvref_t<Sender>>;

  auto result = sync_wait(bexec::into_variant(std::forward<Sender>(sender)));
  if (!result) {
    return std::nullopt;
  }
  return std::optional<variant_type>{std::move(std::get<0>(*result))};
}

}  // namespace bexec::this_thread
#endif  // BEXEC_INCLUDE_BEXEC_SYNC_WAIT_HPP_
