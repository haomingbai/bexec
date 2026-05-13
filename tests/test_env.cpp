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
#include <concepts>
#include <cstddef>
#include <memory>

#include "test_support.hpp"

namespace bexec_tests {

struct allocator_counts {
  int allocations{0};
  int deallocations{0};
};

template <class T>
struct counting_allocator {
  using value_type = T;

  std::shared_ptr<allocator_counts> counts;

  counting_allocator() = default;

  explicit counting_allocator(std::shared_ptr<allocator_counts> counts_value)
      : counts(std::move(counts_value)) {}

  template <class U>
  counting_allocator(const counting_allocator<U>& other) noexcept
      : counts(other.counts) {}

  [[nodiscard]] T* allocate(std::size_t count) {
    if (counts) {
      ++counts->allocations;
    }
    return std::allocator<T>{}.allocate(count);
  }

  void deallocate(T* pointer, std::size_t count) noexcept {
    if (counts) {
      ++counts->deallocations;
    }
    std::allocator<T>{}.deallocate(pointer, count);
  }

  template <class U>
  bool operator==(const counting_allocator<U>& other) const noexcept {
    return counts == other.counts;
  }
};

struct allocator_env {
  counting_allocator<std::byte> allocator;

  [[nodiscard]] counting_allocator<std::byte> query(
      bexec::get_allocator_t) const noexcept {
    return allocator;
  }
};

void test_env() {
  auto fallback_allocator =
      bexec::query(bexec::empty_env{}, bexec::get_allocator);
  static_assert(
      std::same_as<decltype(fallback_allocator), std::allocator<std::byte>>);

  auto counts = std::make_shared<allocator_counts>();
  allocator_env custom_env{counting_allocator<std::byte>{counts}};
  auto custom_allocator = bexec::get_allocator(custom_env);
  auto queried_custom_allocator =
      bexec::query(custom_env, bexec::get_allocator);
  CHECK(custom_allocator.counts == counts);
  CHECK(queried_custom_allocator.counts == counts);

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
