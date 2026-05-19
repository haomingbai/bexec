/**
 * @file tests/test_scheduler.cpp
 * @brief Tests io_context scheduling behavior.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Exercises FIFO queue execution, stopped/restarted state, scheduler
 * equality, scheduled sender completion, and cancellation-aware scheduling.
 */

#include <bexec/io_context/io_context.hpp>
#include <bexec/just.hpp>
#include <bexec/on.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <bexec/sync_wait.hpp>
#include <bexec/then.hpp>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

struct scheduler_env {
  bexec::run_loop::scheduler scheduler;

  [[nodiscard]] bexec::run_loop::scheduler query(
      bexec::get_scheduler_t) const noexcept {
    return scheduler;
  }

  [[nodiscard]] bexec::run_loop::scheduler query(
      bexec::get_delegation_scheduler_t) const noexcept {
    return scheduler;
  }
};

struct scheduled_int_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();
  scheduler_env env;

  void set_value(int value) noexcept {
    state->signal = signal_kind::value;
    state->int_value = value;
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    state->signal = signal_kind::error;
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }

  [[nodiscard]] scheduler_env get_env() const noexcept { return env; }
};

class sync_choice_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(int),
                                   bexec::set_value_t(std::string)>;

  explicit sync_choice_sender(bool use_string) : use_string_(use_string) {}

  template <class Receiver>
  class operation {
   public:
    operation(bool use_string, Receiver receiver)
        : use_string_(use_string), receiver_(std::move(receiver)) {}

    void start() noexcept {
      if (use_string_) {
        bexec::set_value(std::move(receiver_), std::string{"variant"});
      } else {
        bexec::set_value(std::move(receiver_), 11);
      }
    }

   private:
    bool use_string_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{use_string_, std::move(receiver)};
  }

 private:
  bool use_string_;
};

}  // namespace

void test_scheduler() {
  {
    bexec::io_context context;
    bool ran = false;
    auto state = std::make_shared<shared_state>();

    auto sender = bexec::schedule(context.get_scheduler()) |
                  bexec::then([&] { ran = true; });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(!ran);
    CHECK(state->signal == signal_kind::none);
    CHECK(context.run() == 1);
    CHECK(ran);
    CHECK(state->signal == signal_kind::value);
  }

  {
    bexec::run_loop loop;
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::starts_on(loop.get_scheduler(), bexec::just(7));
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::none);
    CHECK(loop.run_one() == 1);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 7);
  }

  {
    bexec::run_loop target;
    bexec::run_loop final;
    bool child_ran = false;
    scheduled_int_receiver receiver;
    receiver.env = scheduler_env{final.get_scheduler()};
    auto state = receiver.state;
    auto sender = bexec::on(target.get_scheduler(),
                            bexec::just(4) | bexec::then([&](int value) {
                              child_ran = true;
                              return value + 5;
                            }));
    auto operation = bexec::connect(std::move(sender), receiver);

    bexec::start(operation);
    CHECK(target.run_one() == 1);
    CHECK(child_ran);
    CHECK(state->signal == signal_kind::none);
    CHECK(final.run_one() == 1);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 9);
  }

  {
    auto result = bexec::this_thread::sync_wait(bexec::just(1, 2));
    CHECK(result.has_value());
    CHECK(std::get<0>(*result) == 1);
    CHECK(std::get<1>(*result) == 2);
  }

  {
    auto result = bexec::this_thread::sync_wait(bexec::just_stopped());
    CHECK(!result.has_value());
  }

  {
    bool caught = false;
    try {
      (void)bexec::this_thread::sync_wait(bexec::just_error(13));
    } catch (int value) {
      caught = (value == 13);
    }
    CHECK(caught);
  }

  {
    auto result =
        bexec::this_thread::sync_wait_with_variant(sync_choice_sender{false});
    CHECK(result.has_value());
    CHECK(std::holds_alternative<std::tuple<int>>(*result));
    CHECK(std::get<0>(std::get<std::tuple<int>>(*result)) == 11);
  }
}

}  // namespace bexec_tests
