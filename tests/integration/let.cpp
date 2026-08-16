/**
 * @file tests/integration/let.cpp
 * @brief Scheduled let_value child integration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/let.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sync_wait.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>
#include <memory>
#include <tuple>

#include "test_support.hpp"

namespace bexec_tests {

TEST(integration, let_replacement_schedules_child_work) {
  bexec::run_loop loop;
  auto sender = bexec::just(40) | bexec::let_value([&](int value) {
                  return bexec::schedule(loop.get_scheduler()) |
                         bexec::then([value] { return value + 2; });
                });

  auto state = std::make_shared<shared_state>();
  auto operation = bexec::connect(std::move(sender), any_receiver{state});
  bexec::start(operation);
  EXPECT_EQ(state->signal, signal_kind::none);
  loop.finish();
  loop.run();
  EXPECT_EQ(state->signal, signal_kind::value);
  EXPECT_EQ(state->int_value, 42);
}

// Regression: a let_* factory returning an env-querying child (when_all reads
// the receiver env for its stop token) used to instantiate
// let_child_receiver::get_env while let_operation was still incomplete.
TEST(integration, let_value_child_signatures_may_query_env) {
  auto sender = bexec::let_value(bexec::just(1), [](int value) {
    return bexec::when_all(bexec::just(value), bexec::just(value + 1));
  });

  auto result = bexec::this_thread::sync_wait(std::move(sender));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 1);
  EXPECT_EQ(std::get<1>(*result), 2);
}

// Regression: same env query through a downstream completion-adaptor receiver
// (let_* followed by then in a pipeline).
TEST(integration, let_value_downstream_adaptor_receiver_env_query) {
  auto sender = bexec::then(
      bexec::let_value(bexec::just(20),
                       [](int value) {
                         return bexec::when_all(bexec::just(value + 1),
                                                bexec::just(value * 2));
                       }),
      [](int plus_one, int doubled) { return plus_one + doubled; });

  auto result = bexec::this_thread::sync_wait(std::move(sender));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 61);
}

}  // namespace bexec_tests
