/**
 * @file tests/test_repeat_until.cpp
 * @brief Tests the repeat_until sender algorithm.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Verifies repeated synchronous work, error and stopped propagation,
 * cancellation checks, and scheduler-based asynchronous repetition.
 */

#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/repeat_until.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <memory>
#include <string>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {

void test_repeat_until() {
  {
    int count = 0;
    auto sender = bexec::repeat_until(
        [&] { return bexec::just() | bexec::then([&] { ++count; }); },
        [&] { return count == 3; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(count == 3);
    CHECK(state->signal == signal_kind::value);
  }

  {
    int count = 0;
    auto sender = bexec::repeat_until(
        [&] { return bexec::just() | bexec::then([&] { ++count; }); },
        [&] { return count == 10000; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(count == 10000);
    CHECK(state->signal == signal_kind::value);
  }

  {
    bexec::run_loop loop;
    int count = 0;
    auto sender = bexec::repeat_until(
        [&] {
          return bexec::schedule(loop.get_scheduler()) |
                 bexec::then([&] { ++count; });
        },
        [&] { return count == 5; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    loop.finish();
    loop.run();
    CHECK(count == 5);
    CHECK(state->signal == signal_kind::value);
  }

  {
    auto sender = bexec::repeat_until(
        [] { return bexec::just_error(std::string{"bad"}); },
        [] { return false; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(state->signal == signal_kind::error);
  }

  {
    auto sender = bexec::repeat_until([] { return bexec::just_stopped(); },
                                      [] { return false; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(state->signal == signal_kind::stopped);
  }
}

}  // namespace bexec_tests
