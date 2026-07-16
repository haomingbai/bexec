/**
 * @file tests/stress/sync_wait.cpp
 * @brief Repeated local sync_wait lifecycle tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/sync_wait.hpp>
#include <tuple>

#include "receiver_scheduler_sender.hpp"
#include "test_support.hpp"

namespace bexec_tests {

TEST(stress, sync_wait_repeated_local_loop_lifecycle) {
  const int iterations = stress_iterations(10000);
  for (int index = 0; index != iterations; ++index) {
    auto result = bexec::this_thread::sync_wait(receiver_scheduler_sender{});
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == 42);
  }
}

}  // namespace bexec_tests
