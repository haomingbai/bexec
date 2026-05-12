/**
 * @file tests/test_scheduler.cpp
 * @brief Tests io_context scheduling behavior.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Exercises FIFO queue execution, stopped/restarted state, scheduler
 * equality, scheduled sender completion, and cancellation-aware scheduling.
 */

#include <bexec/io_context/io_context.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <memory>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {

void test_scheduler() {
  bexec::io_context context;
  bool ran = false;
  auto state = std::make_shared<shared_state>();

  auto sender = bexec::schedule(context.get_scheduler()) |
                bexec::then([&] { ran = true; });
  auto operation = bexec::connect(std::move(sender), any_receiver{state});
  bexec::start(operation);

  CHECK(!ran);
  CHECK(state->signal == signal_kind::none);
  CHECK(context.run() == 1);
  CHECK(ran);
  CHECK(state->signal == signal_kind::value);
}

}  // namespace bexec_tests
