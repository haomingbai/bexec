/**
 * @file tests/stress/into_variant.cpp
 * @brief Repeated into_variant alternative-selection tests.
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

TEST(stress, into_variant_repeated_alternative_selection) {
  const int iterations = stress_iterations(10000);
  for (int index = 0; index != iterations; ++index) {
    const auto selected = (index % 2 == 0) ? choice_sender::outcome::integer
                                           : choice_sender::outcome::string;
    auto result =
        bexec::this_thread::sync_wait_with_variant(choice_sender{selected});
    EXPECT_TRUE(result.has_value());
    if (index % 2 == 0) {
      EXPECT_TRUE(std::holds_alternative<std::tuple<int>>(*result));
    } else {
      EXPECT_TRUE(std::holds_alternative<std::tuple<std::string>>(*result));
    }
  }
}

}  // namespace bexec_tests
