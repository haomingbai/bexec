/**
 * @file tests/basic/into_variant.cpp
 * @brief Basic into_variant completion tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/sync_wait.hpp>
#include <string>
#include <tuple>
#include <variant>

#include "choice_sender.hpp"
#include "test_support.hpp"

namespace bexec_tests {

BEXEC_TEST_CASE(into_variant_value_error_and_stopped_paths, basic) {
  auto integer = bexec::this_thread::sync_wait_with_variant(
      choice_sender{choice_sender::outcome::integer});
  CHECK(integer.has_value());
  CHECK(std::holds_alternative<std::tuple<int>>(*integer));
  CHECK(std::get<0>(std::get<std::tuple<int>>(*integer)) == 42);

  auto string = bexec::this_thread::sync_wait_with_variant(
      choice_sender{choice_sender::outcome::string});
  CHECK(string.has_value());
  CHECK(std::holds_alternative<std::tuple<std::string>>(*string));
  CHECK(std::get<0>(std::get<std::tuple<std::string>>(*string)) == "variant");

  bool caught = false;
  try {
    (void)bexec::this_thread::sync_wait_with_variant(
        choice_sender{choice_sender::outcome::error});
  } catch (int value) {
    caught = value == 7;
  }
  CHECK(caught);

  auto stopped = bexec::this_thread::sync_wait_with_variant(
      choice_sender{choice_sender::outcome::stopped});
  CHECK(!stopped.has_value());
}

}  // namespace bexec_tests
