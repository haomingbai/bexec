/**
 * @file tests/stress/counting_scope.cpp
 * @brief Repeated spawn_future lifecycle stress tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/counting_scope.hpp>
#include <bexec/just.hpp>
#include <bexec/sync_wait.hpp>
#include <tuple>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {

BEXEC_TEST_CASE(counting_scope_repeated_future_lifecycle, stress) {
  const int iterations = stress_iterations(2000);
  for (int index = 0; index != iterations; ++index) {
    bexec::counting_scope scope;
    auto future = bexec::spawn_future(bexec::just(index), scope.get_token());
    auto result = bexec::this_thread::sync_wait(std::move(future));
    CHECK(result.has_value());
    CHECK(std::get<0>(*result) == index);
    scope.close();
    CHECK(bexec::this_thread::sync_wait(scope.join()).has_value());
  }
}

}  // namespace bexec_tests
