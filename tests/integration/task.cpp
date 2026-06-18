/**
 * @file tests/integration/task.cpp
 * @brief Coroutine task movement, exception, and suspension tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/task.hpp>
#include <stdexcept>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

bexec::task<int> throwing_task() {
  throw std::runtime_error{"task failure"};
  co_return 0;
}

bexec::task<int> indexed_task(int value) { co_return value; }

bexec::task<int> twice_suspended_task(int& progress) {
  ++progress;
  co_await std::suspend_always{};
  ++progress;
  co_return 42;
}

}  // namespace

BEXEC_TEST_CASE(task_move_and_exception_propagation, integration) {
  auto original = indexed_task(42);
  auto moved = std::move(original);
  CHECK(original.done());
  CHECK(!moved.done());
  moved.start();
  CHECK(moved.done());
  CHECK(moved.result() == 42);

  auto failure = throwing_task();
  failure.start();
  bool caught = false;
  try {
    (void)failure.result();
  } catch (const std::runtime_error&) {
    caught = true;
  }
  CHECK(caught);

  int progress = 0;
  auto suspended = twice_suspended_task(progress);
  suspended.start();
  CHECK(!suspended.done());
  CHECK(progress == 1);
  suspended.start();
  CHECK(suspended.done());
  CHECK(progress == 2);
  CHECK(suspended.result() == 42);
}

}  // namespace bexec_tests
