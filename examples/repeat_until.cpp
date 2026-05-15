/**
 * @file examples/repeat_until.cpp
 * @brief Demonstrates sequential repetition with repeat_until.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <iostream>

#include "example_receiver.hpp"

int main() {
  int count = 0;

  auto repeated = bexec::repeat_until(
      [&] {
        return bexec::just() | bexec::then([&] {
                 ++count;
                 std::cout << "repeat_until iteration " << count << '\n';
               });
      },
      [&] { return count == 3; });

  bexec_examples::run_sender("repeat_until", std::move(repeated));
}
