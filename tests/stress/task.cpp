/**
 * @file tests/stress/task.cpp
 * @brief Repeated coroutine task lifecycle stress tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/task.hpp>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

bexec::task<int> indexed_task(int value) { co_return value; }

}  // namespace

TEST(stress, task_repeated_create_start_destroy) {
  const int iterations = stress_iterations(50000);
  for (int index = 0; index != iterations; ++index) {
    auto task = indexed_task(index);
    task.start();
    EXPECT_TRUE(task.done());
    EXPECT_TRUE(task.result() == index);
  }
}

}  // namespace bexec_tests
