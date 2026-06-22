/**
 * @file examples/coroutine_task.cpp
 * @brief Demonstrates tasks awaiting senders and synchronous generators.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <iostream>

namespace {

bexec::task<int> scheduled_value(bexec::run_loop& loop) {
  co_await bexec::schedule(loop.get_scheduler());
  co_return co_await bexec::just(41);
}

bexec::task<int> compute_value(bexec::run_loop& loop) {
  co_return (co_await scheduled_value(loop)) + 1;
}

bexec::generator<int> first_values() {
  for (int value = 1; value <= 3; ++value) {
    co_yield value;
  }
}

}  // namespace

int main() {
  bexec::run_loop loop;
  auto task = compute_value(loop);

  task.start();
  loop.finish();
  loop.run();

  std::cout << "coroutine result: " << task.result() << '\n';
  std::cout << "generated values:";
  for (int value : first_values()) {
    std::cout << ' ' << value;
  }
  std::cout << '\n';
}
