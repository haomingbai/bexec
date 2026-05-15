/**
 * @file examples/scheduler.cpp
 * @brief Demonstrates io_context scheduling with sender pipelines.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <iostream>
#include <utility>

#include "example_receiver.hpp"

int main() {
  bexec::io_context context;
  auto scheduler = context.get_scheduler();

  auto first = bexec::schedule(scheduler) | bexec::then([] {
                 std::cout << "scheduled work: first\n";
                 return 1;
               });
  auto second = bexec::schedule(scheduler) | bexec::then([] {
                  std::cout << "scheduled work: second\n";
                  return 2;
                });

  auto first_operation = bexec::connect(
      std::move(first), bexec_examples::logging_receiver{"schedule first"});
  auto second_operation = bexec::connect(
      std::move(second), bexec_examples::logging_receiver{"schedule second"});

  bexec::start(first_operation);
  bexec::start(second_operation);

  auto ran = context.run();
  std::cout << "io_context ran " << ran << " queued item(s)\n";
}
