/**
 * @file include/bexec/io_context/detail/schedule_sender.hpp
 * @brief Internal sender returned by io_context schedulers.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Posts receiver completion to an io_context, observes stop tokens before and
 * after enqueueing, and reports enqueue failures as stopped or error
 * completions.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_IO_CONTEXT_DETAIL_SCHEDULE_SENDER_HPP_
#define BEXEC_INCLUDE_BEXEC_IO_CONTEXT_DETAIL_SCHEDULE_SENDER_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/detail/config.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <exception>
#include <utility>

namespace bexec {
class io_context;
}

namespace bexec::detail {

class schedule_sender {
 public:
  using completion_signatures = bexec::completion_signatures<
      set_value_t(), set_error_t(std::exception_ptr), set_stopped_t()>;

  explicit schedule_sender(io_context& context) : context_(&context) {}

  template <class Receiver>
  class operation {
   public:
    operation(io_context& context, Receiver receiver)
        : context_(&context), receiver_(std::move(receiver)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    void start() noexcept {
      auto token =
          bexec::query(bexec::get_env(receiver_), bexec::get_stop_token);
      if (token.stop_requested()) {
        bexec::set_stopped(std::move(receiver_));
        return;
      }

#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      try {
#endif
        operation* self = this;
        const bool queued =
            context_->post([self]() noexcept { self->complete(); });

        if (!queued) {
          bexec::set_stopped(std::move(receiver_));
        }
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      } catch (...) {
        bexec::set_error(std::move(receiver_), std::current_exception());
      }
#endif
    }

   private:
    void complete() noexcept {
      auto token =
          bexec::query(bexec::get_env(receiver_), bexec::get_stop_token);
      if (token.stop_requested()) {
        bexec::set_stopped(std::move(receiver_));
      } else {
        bexec::set_value(std::move(receiver_));
      }
    }

    io_context* context_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{*context_, std::move(receiver)};
  }

 private:
  io_context* context_;
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_IO_CONTEXT_DETAIL_SCHEDULE_SENDER_HPP_
