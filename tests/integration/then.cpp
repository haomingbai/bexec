/**
 * @file tests/integration/then.cpp
 * @brief Completion-adaptor recovery pipeline tests.
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

TEST(integration, then_error_recovery_pipeline) {
  auto result = bexec::this_thread::sync_wait(
      bexec::just_error(20) |
      bexec::upon_error([](int error) { return error + 1; }) |
      bexec::then([](int value) { return value * 2; }));

  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(std::get<0>(*result) == 42);
}

}  // namespace bexec_tests
