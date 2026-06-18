/**
 * @file tests/integration/on.cpp
 * @brief Cross-scheduler on integration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/env.hpp>
#include <bexec/just.hpp>
#include <bexec/on.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/then.hpp>
#include <memory>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

struct scheduled_state_receiver {
  std::shared_ptr<shared_state> state;
  bexec::env_with_scheduler<bexec::run_loop::scheduler> env;

  void set_value(int value) noexcept {
    state->signal = signal_kind::value;
    state->int_value = value;
  }
  template <class Error>
  void set_error(Error&&) noexcept {
    state->signal = signal_kind::error;
  }
  void set_stopped() noexcept { state->signal = signal_kind::stopped; }
  auto get_env() const noexcept { return env; }
};

}  // namespace

BEXEC_TEST_CASE(on_returns_completion_to_receiver_scheduler, integration) {
  bexec::run_loop target;
  bexec::run_loop final;
  auto state = std::make_shared<shared_state>();
  scheduled_state_receiver receiver{
      state, {final.get_scheduler(), bexec::empty_env{}}};
  auto sender = bexec::on(
      target.get_scheduler(),
      bexec::just(20) | bexec::then([](int value) { return value * 2 + 2; }));
  auto operation = bexec::connect(std::move(sender), receiver);

  bexec::start(operation);
  target.finish();
  target.run();
  CHECK(state->signal == signal_kind::none);
  final.finish();
  final.run();
  CHECK(state->signal == signal_kind::value);
  CHECK(state->int_value == 42);
}

}  // namespace bexec_tests
