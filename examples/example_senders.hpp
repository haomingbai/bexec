/**
 * @file examples/example_senders.hpp
 * @brief Shared sender helpers for bexec examples.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_EXAMPLES_EXAMPLE_SENDERS_HPP_
#define BEXEC_EXAMPLES_EXAMPLE_SENDERS_HPP_

#include <bexec/bexec.hpp>
#include <string>
#include <utility>

namespace bexec_examples {

class choice_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(int),
                                   bexec::set_value_t(std::string)>;

  explicit choice_sender(bool use_string) : use_string_(use_string) {}

  template <class Receiver>
  class operation {
   public:
    operation(bool use_string, Receiver receiver)
        : use_string_(use_string), receiver_(std::move(receiver)) {}

    void start() noexcept {
      if (use_string_) {
        bexec::set_value(std::move(receiver_), std::string{"selected"});
      } else {
        bexec::set_value(std::move(receiver_), 17);
      }
    }

   private:
    bool use_string_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{use_string_, std::move(receiver)};
  }

 private:
  bool use_string_;
};

}  // namespace bexec_examples
#endif  // BEXEC_EXAMPLES_EXAMPLE_SENDERS_HPP_
