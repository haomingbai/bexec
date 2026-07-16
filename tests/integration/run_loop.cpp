/**
 * @file tests/integration/run_loop.cpp
 * @brief Blocking run-loop producer integration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <atomic>
#include <bexec/run_loop.hpp>
#include <bexec/scheduler.hpp>
#include <thread>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

struct atomic_count_receiver {
  std::atomic<int>* count{};

  void set_value() noexcept { count->fetch_add(1, std::memory_order_relaxed); }
  template <class Error>
  void set_error(Error&&) noexcept {}
  void set_stopped() noexcept {}
};

}  // namespace

TEST(integration, run_loop_blocks_and_wakes_for_producer) {
  bexec::run_loop loop;
  std::atomic<int> completed{0};
  auto sender = bexec::schedule(loop.get_scheduler());
  auto operation = bexec::connect(sender, atomic_count_receiver{&completed});

  std::thread runner([&] { loop.run(); });
  bexec::start(operation);
  while (completed.load(std::memory_order_acquire) == 0) {
    std::this_thread::yield();
  }
  loop.finish();
  runner.join();
  EXPECT_EQ(completed.load(std::memory_order_relaxed), 1);
}

}  // namespace bexec_tests
