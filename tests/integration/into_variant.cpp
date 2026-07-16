/**
 * @file tests/integration/into_variant.cpp
 * @brief into_variant and when_all integration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/sync_wait.hpp>
#include <bexec/when_all.hpp>
#include <string>
#include <tuple>
#include <variant>

#include "choice_sender.hpp"
#include "test_support.hpp"

namespace bexec_tests {

TEST(integration, into_variant_composes_with_when_all) {
  auto result = bexec::this_thread::sync_wait(bexec::when_all_with_variant(
      choice_sender{choice_sender::outcome::string},
      choice_sender{choice_sender::outcome::integer}));

  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(
      std::holds_alternative<std::tuple<std::string>>(std::get<0>(*result)));
  EXPECT_TRUE(std::holds_alternative<std::tuple<int>>(std::get<1>(*result)));
}

}  // namespace bexec_tests
