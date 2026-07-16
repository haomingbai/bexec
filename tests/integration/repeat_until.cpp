/**
 * @file tests/integration/repeat_until.cpp
 * @brief Scheduled repeat_until integration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/repeat_until.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/then.hpp>
#include <memory>

#include "test_support.hpp"

namespace bexec_tests {

TEST(integration, repeat_until_scheduled_retry_pipeline) {
  bexec::run_loop loop;
  int attempts = 0;
  auto sender = bexec::repeat_until(
      [&] {
        return bexec::schedule(loop.get_scheduler()) |
               bexec::then([&] { return ++attempts; });
      },
      [&] { return attempts == 4; });

  auto state = std::make_shared<shared_state>();
  auto operation = bexec::connect(std::move(sender), any_receiver{state});
  bexec::start(operation);
  loop.finish();
  loop.run();

  EXPECT_TRUE(state->signal == signal_kind::value);
  EXPECT_TRUE(state->int_value == 4);
  EXPECT_TRUE(attempts == 4);
}

}  // namespace bexec_tests
