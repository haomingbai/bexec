/**
 * @file tests/stress/then.cpp
 * @brief Synchronous completion-adaptor stress tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/sync_wait.hpp>
#include <bexec/then.hpp>
#include <tuple>

#include "test_support.hpp"

namespace bexec_tests {

TEST(stress, then_long_synchronous_workload) {
  const int iterations = stress_iterations(20000);
  for (int index = 0; index != iterations; ++index) {
    auto result = bexec::this_thread::sync_wait(
        bexec::just(index) | bexec::then([](int value) { return value + 1; }) |
        bexec::then([](int value) { return value * 2; }));
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), (index + 1) * 2);
  }
}

}  // namespace bexec_tests
