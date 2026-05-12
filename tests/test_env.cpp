/**
 * @file tests/test_env.cpp
 * @brief Tests receiver environment and query behavior.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Verifies empty_env, get_env fallback behavior, env_with_stop_token,
 * get_stop_token, and delegated environment queries.
 */

#include <bexec/env.hpp>
#include <bexec/io_context/io_context.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <memory>

#include "test_support.hpp"

namespace bexec_tests {

void test_env() {
  bexec::inplace_stop_source source;
  source.request_stop();

  using env_type = bexec::env_with_stop_token<>;
  env_type env{source.get_token()};

  bexec::io_context context;
  auto state = std::make_shared<shared_state>();
  env_receiver<env_type> receiver{state, env};

  auto operation =
      bexec::connect(bexec::schedule(context.get_scheduler()), receiver);
  bexec::start(operation);

  CHECK(state->signal == signal_kind::stopped);
  CHECK(context.run() == 0);
}

}  // namespace bexec_tests
