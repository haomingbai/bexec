/**
 * @file examples/just_then.cpp
 * @brief Demonstrates immediate senders and then value transformations.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <iostream>
#include <string>

#include "example_receiver.hpp"

int main() {
  bexec_examples::run_sender(
      "just value",
      bexec::just(21) | bexec::then([](int value) { return value * 2; }));

  bexec_examples::run_sender("then void", bexec::just() | bexec::then([] {
                                            std::cout
                                                << "then void: side effect\n";
                                          }));

  bexec_examples::run_sender("just error",
                             bexec::just_error(std::string{"failed"}));
  bexec_examples::run_sender("just stopped", bexec::just_stopped());
}
