/**
 * @file tests/basic/into_variant.cpp
 * @brief Basic into_variant completion tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/into_variant.hpp>
#include <bexec/sync_wait.hpp>
#include <string>
#include <tuple>
#include <variant>

#include "choice_sender.hpp"
#include "test_support.hpp"

namespace bexec_tests {

TEST(basic, into_variant_value_error_and_stopped_paths) {
  auto integer = bexec::this_thread::sync_wait_with_variant(
      choice_sender{choice_sender::outcome::integer});
  ASSERT_TRUE(integer.has_value());
  EXPECT_TRUE(std::holds_alternative<std::tuple<int>>(*integer));
  EXPECT_EQ(std::get<0>(std::get<std::tuple<int>>(*integer)), 42);

  auto string = bexec::this_thread::sync_wait_with_variant(
      choice_sender{choice_sender::outcome::string});
  ASSERT_TRUE(string.has_value());
  EXPECT_TRUE(std::holds_alternative<std::tuple<std::string>>(*string));
  EXPECT_EQ(std::get<0>(std::get<std::tuple<std::string>>(*string)), "variant");

  bool caught = false;
  try {
    (void)bexec::this_thread::sync_wait_with_variant(
        choice_sender{choice_sender::outcome::error});
  } catch (int value) {
    caught = value == 7;
  }
  EXPECT_TRUE(caught);

  auto stopped = bexec::this_thread::sync_wait_with_variant(
      choice_sender{choice_sender::outcome::stopped});
  EXPECT_FALSE(stopped.has_value());
}

// Receiver for into_variant tests. It accepts any value type (the throwing
// value-construction path never delivers the value, but the call site inside
// into_variant_receiver::set_value must still be well-formed) and records
// std::exception_ptr errors.
struct into_variant_test_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();

  template <class... Args>
  void set_value(Args&&...) noexcept {
    state->signal = signal_kind::value;
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    state->signal = signal_kind::error;
    if constexpr (std::same_as<std::decay_t<Error>, std::exception_ptr>) {
      state->exception = std::forward<Error>(error);
    }
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }
};

TEST(basic, into_variant_throwing_value) {
  // into_variant_receiver::set_value else-branch (try/catch): constructing the
  // value variant from a throwing_value move throws; the exception is caught
  // and reported to the receiver as an std::exception_ptr error.
  auto state = std::make_shared<shared_state>();
  auto sender = bexec::into_variant(throwing_value_sender{});
  auto op =
      bexec::connect(std::move(sender), into_variant_test_receiver{state});
  bexec::start(op);
  EXPECT_EQ(state->signal, signal_kind::error);
  EXPECT_TRUE(static_cast<bool>(state->exception));
}

}  // namespace bexec_tests
