/**
 * @file tests/integration/when_all.cpp
 * @brief Scheduled when_all pipeline integration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/on.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>
#include <memory>

#include "test_support.hpp"

namespace bexec_tests {

TEST(integration, when_all_scheduled_pipeline) {
  bexec::run_loop loop;
  auto first = bexec::starts_on(
      loop.get_scheduler(),
      bexec::just(20) | bexec::then([](int value) { return value + 1; }));
  auto second = bexec::starts_on(
      loop.get_scheduler(),
      bexec::just(7) | bexec::then([](int value) { return value * 3; }));
  auto sender = bexec::when_all(std::move(first), std::move(second)) |
                bexec::then([](int lhs, int rhs) { return lhs + rhs; });

  auto state = std::make_shared<shared_state>();
  auto operation = bexec::connect(std::move(sender), any_receiver{state});
  bexec::start(operation);
  loop.finish();
  loop.run();

  EXPECT_TRUE(state->signal == signal_kind::value);
  EXPECT_TRUE(state->int_value == 42);
}

}  // namespace bexec_tests
