/**
 * @file tests/basic/generator.cpp
 * @brief Basic generator range and iterator tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/generator.hpp>
#include <concepts>
#include <ranges>
#include <type_traits>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

bexec::generator<int> values() {
  co_yield 1;
  co_yield 2;
  co_yield 3;
}

bexec::generator<int> empty_values() { co_return; }

}  // namespace

TEST(basic, generator_models_single_pass_range) {
  static_assert(std::ranges::input_range<bexec::generator<int>>);
  static_assert(
      std::same_as<std::ranges::range_reference_t<bexec::generator<int>>,
                   const int&>);
  static_assert(!std::copy_constructible<bexec::generator<int>>);
  static_assert(std::movable<bexec::generator<int>>);

  int expected = 1;
  for (int value : values()) {
    EXPECT_EQ(value, expected);
    ++expected;
  }
  EXPECT_EQ(expected, 4);

  int count = 0;
  for ([[maybe_unused]] int value : empty_values()) {
    ++count;
  }
  EXPECT_EQ(count, 0);
}

}  // namespace bexec_tests
