/**
 * @file examples/run_loop.cpp
 * @brief Demonstrates the stack-owned run_loop scheduler.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <iostream>
#include <utility>

#include "example_receiver.hpp"

int main() {
  bexec::run_loop loop;

  auto sender = bexec::starts_on(loop.get_scheduler(), bexec::just(9)) |
                bexec::then([](int value) {
                  std::cout << "run_loop child executed\n";
                  return value + 1;
                });

  auto operation = bexec::connect(std::move(sender),
                                  bexec_examples::logging_receiver{"run_loop"});

  bexec::start(operation);
  std::cout << "run_loop before run\n";
  loop.finish();
  loop.run();
  std::cout << "run_loop drained queued work\n";
}
