/**
 * @file tests/test_stop_token.cpp
 * @brief Tests inplace stop-token primitives.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Verifies never_stop_token, stop requests, callback registration, late
 * registration, callback deactivation, and one-shot invocation behavior.
 */

#include <atomic>
#include <bexec/stop_token.hpp>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

void wait_until(const std::atomic_bool& flag) {
  while (!flag.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

}  // namespace

void test_stop_token() {
  bexec::inplace_stop_source source;
  auto token = source.get_token();

  int callbacks = 0;
  {
    bexec::inplace_stop_callback callback{token, [&] { ++callbacks; }};
    CHECK(!token.stop_requested());
    CHECK(source.request_stop());
    CHECK(callbacks == 1);
  }

  bexec::inplace_stop_callback immediate{token, [&] { ++callbacks; }};
  CHECK(callbacks == 2);
  CHECK(token.stop_requested());

  {
    bexec::inplace_stop_source self_destroy_source;
    auto self_destroy_token = self_destroy_source.get_token();
    int self_destroy_callbacks = 0;

    using callback_type = bexec::inplace_stop_callback<std::function<void()>>;
    std::unique_ptr<callback_type> registration;
    std::function<void()> callback = [&] {
      ++self_destroy_callbacks;
      registration.reset();
    };

    registration =
        std::make_unique<callback_type>(self_destroy_token, callback);
    CHECK(self_destroy_source.request_stop());
    CHECK(self_destroy_callbacks == 1);
    CHECK(registration == nullptr);
  }

  {
    bexec::inplace_stop_source concurrent_source;
    auto concurrent_token = concurrent_source.get_token();
    std::atomic_int concurrent_callbacks{0};
    std::atomic_int request_winners{0};

    bexec::inplace_stop_callback first{
        concurrent_token,
        [&] { concurrent_callbacks.fetch_add(1, std::memory_order_relaxed); }};
    bexec::inplace_stop_callback second{
        concurrent_token,
        [&] { concurrent_callbacks.fetch_add(1, std::memory_order_relaxed); }};

    std::vector<std::thread> requesters;
    requesters.reserve(8);
    for (int index = 0; index != 8; ++index) {
      requesters.emplace_back([&] {
        if (concurrent_source.request_stop()) {
          request_winners.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }

    for (auto& requester : requesters) {
      requester.join();
    }

    CHECK(request_winners.load(std::memory_order_relaxed) == 1);
    CHECK(concurrent_callbacks.load(std::memory_order_relaxed) == 2);
  }

  {
    constexpr int iterations = 2000;

    for (int iteration = 0; iteration != iterations; ++iteration) {
      bexec::inplace_stop_source race_source;
      auto race_token = race_source.get_token();
      std::atomic_bool callback_registered{false};
      std::atomic_bool release_requester{false};
      std::atomic_int iteration_callbacks{0};

      std::thread requester{[&] {
        wait_until(callback_registered);
        wait_until(release_requester);
        (void)race_source.request_stop();
      }};

      {
        bexec::inplace_stop_callback callback{
            race_token, [&] {
              iteration_callbacks.fetch_add(1, std::memory_order_relaxed);
            }};
        callback_registered.store(true, std::memory_order_release);
        release_requester.store(true, std::memory_order_release);
      }

      requester.join();
      CHECK(iteration_callbacks.load(std::memory_order_relaxed) <= 1);
    }
  }

  {
    constexpr int threads = 8;
    bexec::inplace_stop_source late_source;
    auto late_token = late_source.get_token();
    std::atomic_int late_callbacks{0};

    CHECK(late_source.request_stop());

    std::vector<std::thread> registrars;
    registrars.reserve(threads);
    for (int index = 0; index != threads; ++index) {
      registrars.emplace_back([&] {
        bexec::inplace_stop_callback callback{
            late_token,
            [&] { late_callbacks.fetch_add(1, std::memory_order_relaxed); }};
      });
    }

    for (auto& registrar : registrars) {
      registrar.join();
    }

    CHECK(late_callbacks.load(std::memory_order_relaxed) == threads);
  }
}

}  // namespace bexec_tests
