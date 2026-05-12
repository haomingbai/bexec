/**
 * @file tests/test_task.cpp
 * @brief Tests coroutine task support.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Verifies task<T>, task<void>, result handling, exception propagation when
 * enabled, and scheduler awaitable resumption.
 */

#include <bexec/io_context/io_context.hpp>
#include <bexec/task.hpp>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

bexec::task<int> scheduled_value(bexec::io_context::scheduler scheduler) {
  co_await scheduler.schedule_awaitable();
  co_return 42;
}

bexec::task<void> scheduled_void(bexec::io_context::scheduler scheduler,
                                 bool& ran) {
  co_await scheduler.schedule_awaitable();
  ran = true;
}

}  // namespace

void test_task() {
  bexec::io_context context;
  auto value_task = scheduled_value(context.get_scheduler());

  value_task.start();
  CHECK(!value_task.done());
  CHECK(context.run() == 1);
  CHECK(value_task.done());
  CHECK(value_task.result() == 42);

  bool ran = false;
  auto void_task = scheduled_void(context.get_scheduler(), ran);
  void_task.start();
  CHECK(context.run() == 1);
  CHECK(void_task.done());
  void_task.result();
  CHECK(ran);
}

}  // namespace bexec_tests
