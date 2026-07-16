/**
 * @file tests/integration/generator.cpp
 * @brief Generator ranges algorithm and exception tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <bexec/generator.hpp>
#include <stdexcept>
#include <vector>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

bexec::generator<int> sequence(int count) {
  for (int value = 0; value != count; ++value) {
    co_yield value;
  }
}

bexec::generator<int> failing_sequence() {
  co_yield 1;
  throw std::runtime_error{"generator failure"};
}

}  // namespace

TEST(integration, generator_works_with_ranges_algorithms) {
  std::vector<int> collected;
  std::ranges::copy(sequence(5), std::back_inserter(collected));

  EXPECT_TRUE(collected.size() == 5);
  for (std::size_t index = 0; index != collected.size(); ++index) {
    EXPECT_TRUE(collected[index] == static_cast<int>(index));
  }

  bool caught = false;
  try {
    for ([[maybe_unused]] int value : failing_sequence()) {
    }
  } catch (const std::runtime_error&) {
    caught = true;
  }
  EXPECT_TRUE(caught);
}

}  // namespace bexec_tests
