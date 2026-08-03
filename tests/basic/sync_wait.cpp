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
#include <exception>
#include <stdexcept>
#include <tuple>

#include "test_support.hpp"

namespace bexec_tests {

TEST(basic, sync_wait_value_error_and_stopped) {
  auto value = bexec::this_thread::sync_wait(bexec::just(1, 2));
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(std::get<0>(*value), 1);
  EXPECT_EQ(std::get<1>(*value), 2);

  EXPECT_FALSE(
      bexec::this_thread::sync_wait(bexec::just_stopped()).has_value());

  bool caught = false;
  try {
    (void)bexec::this_thread::sync_wait(bexec::just_error(42));
  } catch (int error) {
    caught = error == 42;
  }
  EXPECT_TRUE(caught);
}

TEST(basic, sync_wait_error_construction_and_rethrow) {
  // set_value else-branch: value move construction throws, caught and
  // re-thrown by sync_wait as std::exception_ptr (via throw_error).
  {
    bool caught = false;
    try {
      (void)bexec::this_thread::sync_wait(throwing_value_sender{});
    } catch (const std::runtime_error& error) {
      caught = std::string(error.what()) == "throwing_value move";
    }
    EXPECT_TRUE(caught);
  }

  // throw_error: an std::exception_ptr error is rethrown unchanged.
  {
    bool caught = false;
    try {
      (void)bexec::this_thread::sync_wait(bexec::just_error(
          std::make_exception_ptr(std::runtime_error("rethrown"))));
    } catch (const std::runtime_error& error) {
      caught = std::string(error.what()) == "rethrown";
    }
    EXPECT_TRUE(caught);
  }
}

}  // namespace bexec_tests
