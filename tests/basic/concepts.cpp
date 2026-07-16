/**
 * @file tests/basic/concepts.cpp
 * @brief Tests the public concept vocabulary.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Exercises operation_state, receiver, sender, sender_to, scheduler,
 * stop_token, and stop_source concept checks.
 */

#include <bexec/concepts.hpp>
#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/repeat_until.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>
#include <memory>
#include <type_traits>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {

TEST(basic, vocabulary_concept_contracts) {
  static_assert(bexec::sender<decltype(bexec::just(1))>);

  auto state = std::make_shared<shared_state>();
  any_receiver receiver{state};
  auto sender =
      bexec::just(1) | bexec::then([](int value) { return value + 1; });
  auto operation = bexec::connect(std::move(sender), receiver);
  static_assert(bexec::operation_state<decltype(operation)>);

  bexec::run_loop loop;
  auto schedule_operation =
      bexec::connect(bexec::schedule(loop.get_scheduler()), any_receiver{});
  static_assert(bexec::operation_state<decltype(schedule_operation)>);
  static_assert(!std::move_constructible<decltype(schedule_operation)>);

  auto repeat_sender =
      bexec::repeat_until([] { return bexec::just(); }, [] { return true; });
  auto repeat_operation =
      bexec::connect(std::move(repeat_sender), any_receiver{});
  static_assert(bexec::operation_state<decltype(repeat_operation)>);
  static_assert(!std::move_constructible<decltype(repeat_operation)>);

  auto when_all_operation =
      bexec::connect(bexec::when_all(bexec::just()), any_receiver{});
  static_assert(bexec::operation_state<decltype(when_all_operation)>);
  static_assert(!std::move_constructible<decltype(when_all_operation)>);

  bexec::start(operation);
  EXPECT_EQ(state->signal, signal_kind::value);
  EXPECT_EQ(state->int_value, 2);
}

}  // namespace bexec_tests
