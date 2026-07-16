/**
 * @file tests/basic/repeat_until.cpp
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

TEST(basic, repeat_until_completion_paths) {
  {
    int count = 0;
    auto sender = bexec::repeat_until(
        [&] { return bexec::just() | bexec::then([&] { ++count; }); },
        [&] { return count == 3; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    EXPECT_TRUE(count == 3);
    EXPECT_TRUE(state->signal == signal_kind::value);
  }

  {
    int count = 0;
    auto sender = bexec::repeat_until(
        [&] { return bexec::just() | bexec::then([&] { ++count; }); },
        [&] { return count == 10000; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    EXPECT_TRUE(count == 10000);
    EXPECT_TRUE(state->signal == signal_kind::value);
  }

  {
    bexec::run_loop loop;
    int count = 0;
    auto sender = bexec::repeat_until(
        [&] {
          return bexec::schedule(loop.get_scheduler()) |
                 bexec::then([&] { return ++count; });
        },
        [&] { return count == 5; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    loop.finish();
    loop.run();
    EXPECT_TRUE(count == 5);
    EXPECT_TRUE(state->signal == signal_kind::value);
    EXPECT_TRUE(state->int_value == 5);
  }

  {
    int count = 0;
    auto sender = bexec::repeat_until([&] { return bexec::just(++count); },
                                      [&] { return count == 4; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    EXPECT_TRUE(count == 4);
    EXPECT_TRUE(state->signal == signal_kind::value);
    EXPECT_TRUE(state->int_value == 4);
  }

  {
    int count = 0;
    auto sender = bexec::repeat_until(
        [&] { return bexec::just(std::make_unique<int>(++count)); },
        [&] { return count == 2; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    EXPECT_TRUE(count == 2);
    EXPECT_TRUE(state->signal == signal_kind::value);
    EXPECT_TRUE(state->int_value == 2);
  }

  {
    auto sender = bexec::repeat_until(
        [] { return bexec::just_error(std::string{"bad"}); },
        [] { return false; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    EXPECT_TRUE(state->signal == signal_kind::error);
  }

  {
    auto sender = bexec::repeat_until([] { return bexec::just_stopped(); },
                                      [] { return false; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    EXPECT_TRUE(state->signal == signal_kind::stopped);
  }
}

}  // namespace bexec_tests
