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

TEST(basic, stop_token_registration_and_unregistration) {
  bexec::inplace_stop_source source;
  auto token = source.get_token();
  int callbacks = 0;
  {
    bexec::inplace_stop_callback callback{token, [&] { ++callbacks; }};
  }
  EXPECT_TRUE(source.request_stop());
  EXPECT_TRUE(callbacks == 0);

  bexec::inplace_stop_callback late{token, [&] { ++callbacks; }};
  EXPECT_TRUE(callbacks == 1);
  EXPECT_TRUE(!source.request_stop());
}

}  // namespace bexec_tests
