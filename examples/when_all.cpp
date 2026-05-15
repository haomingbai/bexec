/**
 * @file examples/when_all.cpp
 * @brief Demonstrates structured startup with when_all.
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
  int completed = 0;

  auto first = bexec::schedule(scheduler) | bexec::then([&] {
                 ++completed;
                 std::cout << "when_all child one\n";
               });
  auto second = bexec::schedule(scheduler) | bexec::then([&] {
                  ++completed;
                  std::cout << "when_all child two\n";
                });

  auto operation =
      bexec::connect(bexec::when_all(std::move(first), std::move(second)),
                     bexec_examples::logging_receiver{"when_all"});

  bexec::start(operation);
  context.run();

  std::cout << "when_all completed children: " << completed << '\n';
}
