/**
 * @file tests/stress/run_loop.cpp
 * @brief Multi-producer run-loop stress tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <atomic>
#include <bexec/run_loop.hpp>
#include <bexec/scheduler.hpp>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

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

TEST(stress, run_loop_multi_producer_queue) {
  bexec::run_loop loop;
  std::atomic<int> completed{0};
  auto sender = bexec::schedule(loop.get_scheduler());
  using operation_type =
      decltype(bexec::connect(sender, atomic_count_receiver{&completed}));

  const int thread_count = 8;
  const int operations_per_thread = stress_iterations(1000);
  std::mutex operations_mutex;
  std::vector<std::unique_ptr<operation_type>> operations;
  operations.reserve(
      static_cast<std::size_t>(thread_count * operations_per_thread));

  std::thread runner([&] { loop.run(); });
  std::vector<std::thread> producers;
  for (int thread_index = 0; thread_index != thread_count; ++thread_index) {
    producers.emplace_back([&] {
      for (int index = 0; index != operations_per_thread; ++index) {
        auto operation = std::make_unique<operation_type>(
            loop, atomic_count_receiver{&completed});
        operation_type* raw_operation = operation.get();
        {
          std::lock_guard lock(operations_mutex);
          operations.push_back(std::move(operation));
        }
        bexec::start(*raw_operation);
      }
    });
  }

  for (auto& producer : producers) {
    producer.join();
  }
  loop.finish();
  runner.join();
  EXPECT_TRUE(completed.load(std::memory_order_relaxed) ==
              thread_count * operations_per_thread);
}

}  // namespace bexec_tests
