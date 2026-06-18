/**
 * @file tests/basic/stop_token.cpp
 * @brief Basic stop-token registration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/stop_token.hpp>

#include "test_support.hpp"

namespace bexec_tests {

BEXEC_TEST_CASE(stop_token_registration_and_unregistration, basic) {
  bexec::inplace_stop_source source;
  auto token = source.get_token();
  int callbacks = 0;
  {
    bexec::inplace_stop_callback callback{token, [&] { ++callbacks; }};
  }
  CHECK(source.request_stop());
  CHECK(callbacks == 0);

  bexec::inplace_stop_callback late{token, [&] { ++callbacks; }};
  CHECK(callbacks == 1);
  CHECK(!source.request_stop());
}

}  // namespace bexec_tests
