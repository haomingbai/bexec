/**
 * @file tests/receiver_scheduler_sender.hpp
 * @brief Sender that completes through the receiver's scheduler.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_TESTS_RECEIVER_SCHEDULER_SENDER_HPP_
#define BEXEC_TESTS_RECEIVER_SCHEDULER_SENDER_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/detail/manual_lifetime.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/scheduler.hpp>
#include <utility>

namespace bexec_tests {

class receiver_scheduler_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(int)>;

  template <class Receiver>
  class operation {
   public:
    class schedule_receiver {
     public:
      explicit schedule_receiver(operation& owner) : owner_(&owner) {}

      auto get_env() const noexcept {
        return bexec::get_env(owner_->receiver_);
      }

      void set_value() noexcept {
        bexec::set_value(std::move(owner_->receiver_), 42);
      }

      template <class Error>
      void set_error(Error&& error) noexcept {
        bexec::set_error(std::move(owner_->receiver_),
                         std::forward<Error>(error));
      }

      void set_stopped() noexcept {
        bexec::set_stopped(std::move(owner_->receiver_));
      }

     private:
      operation* owner_;
    };

    using scheduler_type = decltype(bexec::get_scheduler(
        bexec::get_env(std::declval<Receiver&>())));
    using schedule_sender_type =
        decltype(bexec::schedule(std::declval<scheduler_type>()));
    using child_operation_type =
        decltype(bexec::connect(std::declval<schedule_sender_type>(),
                                std::declval<schedule_receiver>()));

    explicit operation(Receiver receiver) : receiver_(std::move(receiver)) {}
    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    void start() noexcept {
      auto scheduler = bexec::get_scheduler(bexec::get_env(receiver_));
      child_.emplace_from([&] {
        return bexec::connect(bexec::schedule(scheduler),
                              schedule_receiver{*this});
      });
      bexec::start(*child_);
    }

   private:
    friend class schedule_receiver;
    Receiver receiver_;
    bexec::detail::manual_lifetime<child_operation_type> child_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{std::move(receiver)};
  }
};

}  // namespace bexec_tests

#endif  // BEXEC_TESTS_RECEIVER_SCHEDULER_SENDER_HPP_
