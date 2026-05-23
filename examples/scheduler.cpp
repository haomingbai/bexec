/**
 * @file examples/scheduler.cpp
 * @brief Demonstrates run_loop scheduling with sender pipelines.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <cstddef>
#include <iostream>
#include <utility>

#include "example_receiver.hpp"

int main() {
  bexec::run_loop loop;
  auto scheduler = loop.get_scheduler();

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

  std::size_t ran = 0;
  while (loop.run_one() != 0) {
    ++ran;
  }
  std::cout << "run_loop ran " << ran << " queued item(s)\n";
}
