/**
 * @file tests/stress/just.cpp
 * @brief Repeated move-only just delivery tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <memory>

#include "test_support.hpp"

namespace bexec_tests {

TEST(stress, just_repeated_move_only_delivery) {
  const int iterations = stress_iterations(20000);
  for (int index = 0; index != iterations; ++index) {
    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(bexec::just(std::make_unique<int>(index)),
                                    any_receiver{state});
    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::value);
    EXPECT_EQ(state->int_value, index);
  }
}

}  // namespace bexec_tests
