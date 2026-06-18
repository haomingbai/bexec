/**
 * @file tests/stress/repeat_until.cpp
 * @brief Large synchronous repeat_until stress tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/repeat_until.hpp>
#include <bexec/sync_wait.hpp>
#include <tuple>

#include "test_support.hpp"

namespace bexec_tests {

BEXEC_TEST_CASE(repeat_until_large_synchronous_iteration_count, stress) {
  const int target = stress_iterations(100000);
  int attempts = 0;
  auto result = bexec::this_thread::sync_wait(
      bexec::repeat_until([&] { return bexec::just(++attempts); },
                          [&] { return attempts == target; }));

  CHECK(result.has_value());
  CHECK(std::get<0>(*result) == target);
  CHECK(attempts == target);
}

}  // namespace bexec_tests
