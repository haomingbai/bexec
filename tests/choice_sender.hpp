/**
 * @file tests/choice_sender.hpp
 * @brief Sender with selectable value, error, and stopped completions.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_TESTS_CHOICE_SENDER_HPP_
#define BEXEC_TESTS_CHOICE_SENDER_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/receiver.hpp>
#include <string>
#include <utility>

namespace bexec_tests {

class choice_sender {
 public:
  enum class outcome { integer, string, error, stopped };

  using completion_signatures = bexec::completion_signatures<
      bexec::set_value_t(int), bexec::set_value_t(std::string),
      bexec::set_error_t(int), bexec::set_stopped_t()>;

  explicit choice_sender(outcome selected) : selected_(selected) {}

  template <class Receiver>
  class operation {
   public:
    operation(outcome selected, Receiver receiver)
        : selected_(selected), receiver_(std::move(receiver)) {}

    void start() noexcept {
      switch (selected_) {
        case outcome::integer:
          bexec::set_value(std::move(receiver_), 42);
          break;
        case outcome::string:
          bexec::set_value(std::move(receiver_), std::string{"variant"});
          break;
        case outcome::error:
          bexec::set_error(std::move(receiver_), 7);
          break;
        case outcome::stopped:
          bexec::set_stopped(std::move(receiver_));
          break;
      }
    }

   private:
    outcome selected_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{selected_, std::move(receiver)};
  }

 private:
  outcome selected_;
};

}  // namespace bexec_tests

#endif  // BEXEC_TESTS_CHOICE_SENDER_HPP_
