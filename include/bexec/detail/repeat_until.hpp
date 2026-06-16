/**
 * @file include/bexec/detail/repeat_until.hpp
 * @brief Internal repeat_until operation state machine.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Implements an epoch-counter protocol that coordinates the drain thread and
 * child-completion thread after every child start().
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_REPEAT_UNTIL_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_REPEAT_UNTIL_HPP_

#include <atomic>
#include <bexec/completion_signatures.hpp>
#include <bexec/detail/config.hpp>
#include <bexec/detail/manual_lifetime.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/sender.hpp>
#include <cassert>
#include <cstdint>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace bexec::detail {

template <class Factory, class Predicate, class Receiver>
class repeat_until_operation;

template <class Factory, class Predicate, class Receiver>
class repeat_until_child_receiver {
 public:
  using parent_type = repeat_until_operation<Factory, Predicate, Receiver>;

  repeat_until_child_receiver(parent_type& parent, std::uint64_t epoch)
      : parent_(&parent), child_epoch_(epoch) {}

  [[nodiscard]] auto get_env() const noexcept {
    return bexec::get_env(parent_->receiver());
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
    parent_->child_value(child_epoch_, std::forward<Args>(args)...);
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    parent_->child_error(child_epoch_, std::forward<Error>(error));
  }

  void set_stopped() noexcept { parent_->child_stopped(child_epoch_); }

 private:
  parent_type* parent_;
  std::uint64_t child_epoch_;
};

template <class Factory, class Predicate, class Receiver>
class repeat_until_operation {
 public:
  using sender_type = std::invoke_result_t<Factory&>;
  using child_receiver_type =
      repeat_until_child_receiver<Factory, Predicate, Receiver>;
  using child_operation_type = decltype(bexec::connect(
      std::declval<sender_type>(), std::declval<child_receiver_type>()));
  using child_completions = sender_completion_signatures_t<sender_type>;
  using stored_completion_signatures = completion_signatures_from_type_list_t<
      unique_type_list_t<concat_type_lists_t<
          completion_signatures_to_type_list_t<child_completions>,
          type_list<set_error_t(std::exception_ptr)>>>>;
  using stored_completion = completion_variant_t<stored_completion_signatures>;

  repeat_until_operation(Factory factory, Predicate predicate,
                         Receiver receiver)
      : factory_(std::move(factory)),
        predicate_(std::move(predicate)),
        receiver_(std::move(receiver)) {}

  repeat_until_operation(const repeat_until_operation&) = delete;
  repeat_until_operation& operator=(const repeat_until_operation&) = delete;
  repeat_until_operation(repeat_until_operation&&) = delete;
  repeat_until_operation& operator=(repeat_until_operation&&) = delete;

  Receiver& receiver() noexcept { return receiver_; }

  void start() noexcept { drain(); }

  template <class... Args>
  void child_value(std::uint64_t child_epoch, Args&&... args) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      store_value(std::forward<Args>(args)...);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      store_exception(std::current_exception());
    }
#endif
    finish_child(child_epoch);
  }

  template <class Error>
  void child_error(std::uint64_t child_epoch, Error&& error) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      store_error(std::forward<Error>(error));
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      store_exception(std::current_exception());
    }
#endif
    finish_child(child_epoch);
  }

  void child_stopped(std::uint64_t child_epoch) noexcept {
    stored_.emplace(std::in_place_type<stopped_completion>);
    finish_child(child_epoch);
  }

 private:
  template <class... Args>
  void store_value(Args&&... args) {
    using tuple_type = decayed_tuple<Args...>;
    stored_.emplace(std::in_place_type<value_completion<tuple_type>>,
                    tuple_type{std::forward<Args>(args)...});
  }

  template <class Error>
  void store_error(Error&& error) {
    using error_type = std::decay_t<Error>;
    stored_.emplace(std::in_place_type<error_completion<error_type>>,
                    std::forward<Error>(error));
  }

  void store_exception(std::exception_ptr error) noexcept {
    stored_.emplace(std::in_place_type<error_completion<std::exception_ptr>>,
                    std::move(error));
  }

  void finish_child(std::uint64_t child_epoch) noexcept {
    std::uint64_t expected = child_epoch;
    if (epoch_.compare_exchange_strong(expected, child_epoch + 1,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
      return;
    }

    if (process_stored_completion()) {
      drain();
    }
  }

  bool process_stored_completion() noexcept {
    assert(stored_.has_value());
    bool should_continue = std::visit(
        [this](auto& completion) noexcept -> bool {
          return process_one(completion);
        },
        *stored_);
    if (should_continue) {
      stored_.reset();
    }
    return should_continue;
  }

  template <class Tuple>
  bool process_one(value_completion<Tuple>& completion) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      if (predicate_()) {
        complete_value(std::move(completion.values));
        return false;
      }
      return true;
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      bexec::set_error(std::move(receiver_), std::current_exception());
      return false;
    }
#endif
  }

  template <class Error>
  bool process_one(error_completion<Error>& completion) noexcept {
    bexec::set_error(std::move(receiver_), std::move(completion.error));
    return false;
  }

  bool process_one(stopped_completion&) noexcept {
    bexec::set_stopped(std::move(receiver_));
    return false;
  }

  template <class Tuple>
  void complete_value(Tuple&& tuple) noexcept {
    std::apply(
        [this](auto&&... args) noexcept {
          bexec::set_value(std::move(receiver_),
                           std::forward<decltype(args)>(args)...);
        },
        std::forward<Tuple>(tuple));
  }

  void drain() noexcept {
    for (;;) {
      current_.reset();

      auto token =
          bexec::query(bexec::get_env(receiver_), bexec::get_stop_token);
      if (token.stop_requested()) {
        bexec::set_stopped(std::move(receiver_));
        break;
      }

      const std::uint64_t child_epoch =
          epoch_.load(std::memory_order_acquire);

#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      try {
#endif
        current_.emplace_from([this, child_epoch] {
          auto sender = factory_();
          return bexec::connect(std::move(sender),
                                child_receiver_type{*this, child_epoch});
        });

        bexec::start(*current_);

        std::uint64_t expected = child_epoch;
        if (epoch_.compare_exchange_strong(expected, child_epoch + 1,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
          break;
        }

        if (!process_stored_completion()) {
          break;
        }
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      } catch (...) {
        bexec::set_error(std::move(receiver_), std::current_exception());
        break;
      }
#endif
    }
  }

  Factory factory_;
  Predicate predicate_;
  Receiver receiver_;
  manual_lifetime<child_operation_type> current_;
  std::optional<stored_completion> stored_;
  std::atomic<std::uint64_t> epoch_{0};
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_REPEAT_UNTIL_HPP_
