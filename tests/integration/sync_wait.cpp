/**
 * @file tests/integration/sync_wait.cpp
 * @brief Receiver-scheduler sync_wait integration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/sync_wait.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>
#include <tuple>

#include "receiver_scheduler_sender.hpp"
#include "test_support.hpp"

namespace bexec_tests {

BEXEC_TEST_CASE(sync_wait_drives_receiver_scheduler, integration) {
  auto asynchronous =
      bexec::this_thread::sync_wait(receiver_scheduler_sender{});
  CHECK(asynchronous.has_value());
  CHECK(std::get<0>(*asynchronous) == 42);

  auto composed = bexec::this_thread::sync_wait(
      bexec::when_all(bexec::just(20), receiver_scheduler_sender{}) |
      bexec::then([](int lhs, int rhs) { return lhs + rhs - 20; }));
  CHECK(composed.has_value());
  CHECK(std::get<0>(*composed) == 42);
}

}  // namespace bexec_tests
