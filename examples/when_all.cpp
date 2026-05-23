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
#include <string>
#include <utility>

#include "example_receiver.hpp"
#include "example_senders.hpp"

int main() {
  bexec::run_loop loop;
  auto scheduler = loop.get_scheduler();
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
  while (loop.run_one() != 0) {
  }

  std::cout << "when_all completed children: " << completed << '\n';

  bexec_examples::run_sender(
      "when_all values",
      bexec::when_all(bexec::just(1, 2), bexec::just(std::string{"ok"})));

  bexec_examples::run_sender(
      "when_all_with_variant",
      bexec::when_all_with_variant(bexec_examples::choice_sender{false},
                                   bexec_examples::choice_sender{true}));
}
