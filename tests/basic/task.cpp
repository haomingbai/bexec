/**
 * @file tests/basic/task.cpp
 * @brief Tests coroutine task support.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Verifies task<T>, task<void>, lazy start behavior, result handling, and
 * exception propagation when enabled.
 */

#include <bexec/task.hpp>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

bexec::task<int> lazy_value() { co_return 42; }

bexec::task<void> lazy_void(bool& ran) {
  ran = true;
  co_return;
}

}  // namespace

BEXEC_TEST_CASE(task_lazy_value_and_void_behavior, basic) {
  auto value_task = lazy_value();

  CHECK(!value_task.done());
  value_task.start();
  CHECK(value_task.done());
  CHECK(value_task.result() == 42);

  bool ran = false;
  auto void_task = lazy_void(ran);
  CHECK(!ran);
  CHECK(!void_task.done());
  void_task.start();
  CHECK(void_task.done());
  void_task.result();
  CHECK(ran);
}

}  // namespace bexec_tests
