/**
 * @file tests/stress/generator.cpp
 * @brief Repeated generator iteration stress tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/generator.hpp>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

bexec::generator<int> pair(int value) {
  co_yield value;
  co_yield value + 1;
}

}  // namespace

TEST(stress, generator_repeated_create_and_iterate) {
  const int iterations = stress_iterations(25000);
  long long sum = 0;

  for (int index = 0; index != iterations; ++index) {
    for (int value : pair(index)) {
      sum += value;
    }
  }

  EXPECT_EQ(sum, static_cast<long long>(iterations) *
                     static_cast<long long>(iterations));
}

}  // namespace bexec_tests
