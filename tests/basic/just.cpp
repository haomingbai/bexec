/**
 * @file tests/basic/just.cpp
 * @brief Tests synchronous just-family senders.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Exercises just, just_error, just_stopped, move-only value delivery,
 * copyable lvalue connection, and terminal receiver signals.
 */

#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <memory>
#include <string>

#include "test_support.hpp"

namespace bexec_tests {

TEST(basic, just_completion_paths) {
  {
    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(bexec::just(std::make_unique<int>(42)),
                                    any_receiver{state});

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::value);
    EXPECT_EQ(state->int_value, 42);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(bexec::just_error(std::string{"failed"}),
                                    any_receiver{state});

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::error);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(bexec::just_stopped(), any_receiver{state});

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::stopped);
  }
}

}  // namespace bexec_tests
