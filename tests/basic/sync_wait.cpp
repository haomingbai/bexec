/**
 * @file tests/basic/sync_wait.cpp
 * @brief Basic sync_wait completion tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/sync_wait.hpp>
#include <tuple>

#include "test_support.hpp"

namespace bexec_tests {

BEXEC_TEST_CASE(sync_wait_value_error_and_stopped, basic) {
  auto value = bexec::this_thread::sync_wait(bexec::just(1, 2));
  CHECK(value.has_value());
  CHECK(std::get<0>(*value) == 1);
  CHECK(std::get<1>(*value) == 2);

  CHECK(!bexec::this_thread::sync_wait(bexec::just_stopped()).has_value());

  bool caught = false;
  try {
    (void)bexec::this_thread::sync_wait(bexec::just_error(42));
  } catch (int error) {
    caught = error == 42;
  }
  CHECK(caught);
}

}  // namespace bexec_tests
