/**
 * @file tests/basic/run_loop.cpp
 * @brief Basic run-loop FIFO and finish tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/run_loop.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/then.hpp>
#include <vector>

#include "test_support.hpp"

namespace bexec_tests {

TEST(basic, run_loop_fifo_and_finish) {
  bexec::run_loop loop;
  std::vector<int> order;
  auto scheduler = loop.get_scheduler();
  auto first =
      bexec::schedule(scheduler) | bexec::then([&] { order.push_back(1); });
  auto second =
      bexec::schedule(scheduler) | bexec::then([&] { order.push_back(2); });
  auto third =
      bexec::schedule(scheduler) | bexec::then([&] { order.push_back(3); });
  auto first_operation = bexec::connect(std::move(first), any_receiver{});
  auto second_operation = bexec::connect(std::move(second), any_receiver{});
  auto third_operation = bexec::connect(std::move(third), any_receiver{});

  bexec::start(first_operation);
  bexec::start(second_operation);
  bexec::start(third_operation);
  loop.finish();
  loop.run();
  EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

}  // namespace bexec_tests
