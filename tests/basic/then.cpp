/**
 * @file tests/basic/then.cpp
 * @brief Tests the then sender adaptor.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Exercises value transformation, void-returning callables, exception-to-
 * error conversion, pipe syntax, and direct adaptor usage.
 */

#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <memory>
#include <stdexcept>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {

BEXEC_TEST_CASE(then_completion_transformations, basic) {
  {
    auto state = std::make_shared<shared_state>();
    bool called = false;
    auto sender = bexec::just(5) | bexec::then([&](int) { called = true; });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(called);
    CHECK(state->signal == signal_kind::value);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just() | bexec::then([] {
                    throw std::runtime_error("boom");
                    return 1;
                  });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
    CHECK(static_cast<bool>(state->exception));
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just_error(6) |
                  bexec::upon_error([](int value) { return value + 1; });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 7);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::upon_error(bexec::just_error(3),
                                    [](int value) { return value + 2; });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 5);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender =
        bexec::just_stopped() | bexec::upon_stopped([] { return 42; });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 42);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just_error(1) | bexec::upon_error([](int) {
                    throw std::runtime_error("boom");
                    return 0;
                  });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
    CHECK(static_cast<bool>(state->exception));
  }
}

}  // namespace bexec_tests
