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
#include <bexec/then.hpp>
#include <memory>

#include "test_support.hpp"

namespace bexec_tests {

BEXEC_TEST_CASE(let_replacement_schedules_child_work, integration) {
  bexec::run_loop loop;
  auto sender = bexec::just(40) | bexec::let_value([&](int value) {
                  return bexec::schedule(loop.get_scheduler()) |
                         bexec::then([value] { return value + 2; });
                });

  auto state = std::make_shared<shared_state>();
  auto operation = bexec::connect(std::move(sender), any_receiver{state});
  bexec::start(operation);
  CHECK(state->signal == signal_kind::none);
  loop.finish();
  loop.run();
  CHECK(state->signal == signal_kind::value);
  CHECK(state->int_value == 42);
}

}  // namespace bexec_tests
