/**
 * @file tests/integration/stop_token.cpp
 * @brief Stop-token and queued scheduler integration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/env.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/scheduler.hpp>
#include <memory>

#include "test_support.hpp"

namespace bexec_tests {

TEST(integration, stop_token_cancels_queued_schedule) {
  bexec::run_loop loop;
  bexec::inplace_stop_source source;
  auto state = std::make_shared<shared_state>();
  using env_type = bexec::env_with_stop_token<>;
  env_receiver<env_type> receiver{state, env_type{source.get_token()}};
  auto operation =
      bexec::connect(bexec::schedule(loop.get_scheduler()), receiver);

  bexec::start(operation);
  EXPECT_TRUE(source.request_stop());
  loop.finish();
  loop.run();
  EXPECT_EQ(state->signal, signal_kind::stopped);
}

}  // namespace bexec_tests
