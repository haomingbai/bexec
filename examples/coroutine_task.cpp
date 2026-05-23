/**
 * @file examples/coroutine_task.cpp
 * @brief Demonstrates the lazy task<T> coroutine helper.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <iostream>

namespace {

bexec::task<int> compute_value() { co_return 42; }

}  // namespace

int main() {
  auto task = compute_value();

  task.start();

  std::cout << "coroutine result: " << task.result() << '\n';
}
