/**
 * @file tests/test_when_all.cpp
 * @brief Tests the when_all sender algorithm.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Verifies all-success completion, error variant aggregation, first-error
 * selection, stopped propagation, and scheduler-based child completion.
 */

#include <bexec/io_context/io_context.hpp>
#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "test_support.hpp"

namespace bexec_tests {

void test_when_all() {
  {
    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(
        bexec::when_all(bexec::just(), bexec::just()), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
  }

  {
    using sender_type =
        decltype(bexec::when_all(bexec::just(), bexec::just_error(7)));
    using variant_type = sender_type::error_variant;
    static_assert(std::variant_size_v<variant_type> >= 2);

    variant_receiver<variant_type> receiver;
    auto state = receiver.state;
    auto error = receiver.error;
    auto operation = bexec::connect(
        bexec::when_all(bexec::just(), bexec::just_error(7)), receiver);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
    CHECK(error->has_value());
    CHECK(std::holds_alternative<int>(**error));
    CHECK(std::get<int>(**error) == 7);
  }

  {
    using sender_type = decltype(bexec::when_all(
        bexec::just_error(3), bexec::just_error(std::string{"later"})));
    using variant_type = sender_type::error_variant;
    static_assert(std::variant_size_v<variant_type> >= 3);

    variant_receiver<variant_type> receiver;
    auto state = receiver.state;
    auto error = receiver.error;
    auto operation =
        bexec::connect(bexec::when_all(bexec::just_error(3),
                                       bexec::just_error(std::string{"later"})),
                       receiver);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
    CHECK(error->has_value());
    CHECK(std::holds_alternative<int>(**error));
    CHECK(std::get<int>(**error) == 3);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto operation =
        bexec::connect(bexec::when_all(bexec::just(), bexec::just_stopped()),
                       any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::stopped);
  }

  {
    bexec::io_context context;
    int count = 0;

    auto first = bexec::schedule(context.get_scheduler()) |
                 bexec::then([&] { ++count; });
    auto second = bexec::schedule(context.get_scheduler()) |
                  bexec::then([&] { ++count; });
    auto state = std::make_shared<shared_state>();
    auto operation =
        bexec::connect(bexec::when_all(std::move(first), std::move(second)),
                       any_receiver{state});

    bexec::start(operation);
    CHECK(context.run() == 2);
    CHECK(count == 2);
    CHECK(state->signal == signal_kind::value);
  }
}

}  // namespace bexec_tests
