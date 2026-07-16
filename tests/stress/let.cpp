/**
 * @file tests/stress/let.cpp
 * @brief Repeated let child replacement tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/let.hpp>
#include <bexec/sync_wait.hpp>
#include <tuple>

#include "test_support.hpp"

namespace bexec_tests {

TEST(stress, let_repeated_child_replacement) {
  const int iterations = stress_iterations(10000);
  for (int index = 0; index != iterations; ++index) {
    auto result = bexec::this_thread::sync_wait(
        bexec::just(index) |
        bexec::let_value([](int value) { return bexec::just(value + 1); }));
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == index + 1);
  }
}

}  // namespace bexec_tests
