/**
 * @file examples/let.cpp
 * @brief Demonstrates let_value, let_error, and let_stopped adaptors.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <memory>
#include <string>
#include <utility>

#include "example_receiver.hpp"

int main() {
  bexec_examples::run_sender("let_value",
                             bexec::just(2) | bexec::let_value([](int value) {
                               return bexec::just(value + 40);
                             }));

  bexec_examples::run_sender(
      "let_value move-only",
      bexec::just(std::make_unique<int>(20)) |
          bexec::let_value([](std::unique_ptr<int> value) {
            *value += 22;
            return bexec::just(std::move(value));
          }));

  bexec_examples::run_sender(
      "let_error",
      bexec::just_error(std::string{"missing config"}) |
          bexec::let_error([](std::string reason) {
            return bexec::just(std::string{"recovered from "} + reason);
          }));

  bexec_examples::run_sender(
      "let_stopped", bexec::just_stopped() |
                         bexec::let_stopped([] { return bexec::just(7); }));
}
