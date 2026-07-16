/**
 * @file tests/integration/counting_scope.cpp
 * @brief spawn_future consumption and join integration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/counting_scope.hpp>
#include <bexec/just.hpp>
#include <bexec/sync_wait.hpp>
#include <tuple>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {

TEST(integration, counting_scope_spawn_future_consume_and_join) {
  bexec::counting_scope scope;
  auto future = bexec::spawn_future(bexec::just(42), scope.get_token());
  auto result = bexec::this_thread::sync_wait(std::move(future));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 42);

  scope.close();
  auto joined = bexec::this_thread::sync_wait(scope.join());
  EXPECT_TRUE(joined.has_value());
}

}  // namespace bexec_tests
