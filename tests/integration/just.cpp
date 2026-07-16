/**
 * @file tests/integration/just.cpp
 * @brief just, then, and sync_wait integration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/sync_wait.hpp>
#include <bexec/then.hpp>
#include <memory>
#include <tuple>

#include "test_support.hpp"

namespace bexec_tests {

TEST(integration, just_composes_with_then_and_sync_wait) {
  auto result = bexec::this_thread::sync_wait(
      bexec::just(std::make_unique<int>(40)) |
      bexec::then([](std::unique_ptr<int> value) { return *value + 2; }));

  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(std::get<0>(*result) == 42);
}

}  // namespace bexec_tests
