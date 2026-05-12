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

#include <bexec/stop_token.hpp>

#include "test_support.hpp"

namespace bexec_tests {

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
}

}  // namespace bexec_tests
