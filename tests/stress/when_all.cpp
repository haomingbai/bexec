/**
 * @file tests/stress/when_all.cpp
 * @brief Repeated when_all fan-in stress tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/sync_wait.hpp>
#include <bexec/when_all.hpp>
#include <tuple>

#include "test_support.hpp"

namespace bexec_tests {

TEST(stress, when_all_repeated_fan_in) {
  const int iterations = stress_iterations(10000);
  for (int index = 0; index != iterations; ++index) {
    auto result = bexec::this_thread::sync_wait(bexec::when_all(
        bexec::just(index), bexec::just(index + 1), bexec::just(index + 2)));
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == index);
    EXPECT_TRUE(std::get<1>(*result) == index + 1);
    EXPECT_TRUE(std::get<2>(*result) == index + 2);
  }
}

}  // namespace bexec_tests
