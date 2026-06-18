/**
 * @file tests/basic/on.cpp
 * @brief Basic starts_on scheduling tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/on.hpp>
#include <bexec/run_loop.hpp>
#include <memory>

#include "test_support.hpp"

namespace bexec_tests {

BEXEC_TEST_CASE(starts_on_defers_source_start, basic) {
  bexec::run_loop loop;
  auto state = std::make_shared<shared_state>();
  auto sender = bexec::starts_on(loop.get_scheduler(), bexec::just(42));
  auto operation = bexec::connect(std::move(sender), any_receiver{state});

  bexec::start(operation);
  CHECK(state->signal == signal_kind::none);
  loop.finish();
  loop.run();
  CHECK(state->signal == signal_kind::value);
  CHECK(state->int_value == 42);
}

}  // namespace bexec_tests
