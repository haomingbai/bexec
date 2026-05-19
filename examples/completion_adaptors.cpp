/**
 * @file examples/completion_adaptors.cpp
 * @brief Demonstrates then, upon_error, and upon_stopped.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <stdexcept>
#include <string>

#include "example_receiver.hpp"

int main() {
  bexec_examples::run_sender(
      "then value",
      bexec::just(21) | bexec::then([](int value) { return value * 2; }));

  bexec_examples::run_sender(
      "upon_error", bexec::just_error(5) |
                        bexec::upon_error([](int code) { return code + 100; }));

  bexec_examples::run_sender("upon_stopped",
                             bexec::just_stopped() | bexec::upon_stopped([] {
                               return std::string{"continued"};
                             }));

  bexec_examples::run_sender("then exception",
                             bexec::just() | bexec::then([] {
                               throw std::runtime_error{"boom"};
                               return 0;
                             }));
}
