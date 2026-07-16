/**
 * @file tests/stress/on.cpp
 * @brief Cross-scheduler on stress tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <atomic>
#include <bexec/env.hpp>
#include <bexec/just.hpp>
#include <bexec/on.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/then.hpp>
#include <memory>
#include <vector>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

struct scheduled_count_receiver {
  std::atomic<int>* count{};
  bexec::env_with_scheduler<bexec::run_loop::scheduler> env;

  void set_value(int) noexcept {
    count->fetch_add(1, std::memory_order_relaxed);
  }
  template <class Error>
  void set_error(Error&&) noexcept {}
  void set_stopped() noexcept {}
  auto get_env() const noexcept { return env; }
};

}  // namespace

TEST(stress, on_many_cross_scheduler_completions) {
  bexec::run_loop target;
  bexec::run_loop final;
  std::atomic<int> completed{0};
  const int operation_count = stress_iterations(5000);
  auto transform = [](int value) { return value + 1; };
  using sender_type = decltype(bexec::on(
      target.get_scheduler(), bexec::just(0) | bexec::then(transform)));
  using operation_type = decltype(bexec::connect(
      std::declval<sender_type>(),
      scheduled_count_receiver{&completed,
                               {final.get_scheduler(), bexec::empty_env{}}}));
  std::vector<std::unique_ptr<operation_type>> operations;
  operations.reserve(static_cast<std::size_t>(operation_count));

  for (int index = 0; index != operation_count; ++index) {
    auto operation = std::make_unique<operation_type>(
        target.get_scheduler(), bexec::just(index) | bexec::then(transform),
        scheduled_count_receiver{&completed,
                                 {final.get_scheduler(), bexec::empty_env{}}});
    bexec::start(*operation);
    operations.push_back(std::move(operation));
  }

  target.finish();
  target.run();
  final.finish();
  final.run();
  EXPECT_TRUE(completed.load(std::memory_order_relaxed) == operation_count);
}

}  // namespace bexec_tests
