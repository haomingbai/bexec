/**
 * @file examples/coroutine_task.cpp
 * @brief Demonstrates task<T> with scheduler awaitables.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <iostream>

namespace {

bexec::task<int> compute_on(bexec::io_context::scheduler scheduler) {
  co_await scheduler.schedule_awaitable();
  co_return 42;
}

}  // namespace

int main() {
  bexec::io_context context;
  auto task = compute_on(context.get_scheduler());

  task.start();
  context.run();

  std::cout << "coroutine result: " << task.result() << '\n';
}
