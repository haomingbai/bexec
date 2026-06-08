/**
 * @file tests/test_let.cpp
 * @brief Tests the let sender adaptors.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Exercises let_value, let_error, and let_stopped replacement semantics,
 * forwarding behavior, exception-to-error conversion, move-only values, and
 * support for non-movable operation states.
 */

#include <bexec/just.hpp>
#include <bexec/let.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/receiver.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

class non_movable_value_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(int)>;

  explicit non_movable_value_sender(int value) : value_(value) {}

  template <class Receiver>
  class operation {
   public:
    operation(int value, Receiver receiver)
        : value_(value), receiver_(std::move(receiver)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    void start() noexcept { bexec::set_value(std::move(receiver_), value_); }

   private:
    int value_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return operation<Receiver>{value_, std::move(receiver)};
  }

 private:
  int value_;
};

}  // namespace

void test_let() {
  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::let_value(
        bexec::just(2), [](int value) { return bexec::just(value + 3); });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 5);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender =
        bexec::just(1, 2) | bexec::let_value([](int first, int second) {
          return bexec::just(first + second);
        });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 3);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just_error(7) | bexec::let_value([](int value) {
                    return bexec::just(value + 1);
                  });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender =
        bexec::just_stopped() | bexec::let_value([] { return bexec::just(1); });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::stopped);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just_error(std::string{"failed"}) |
                  bexec::let_error([](std::string value) {
                    return bexec::just(static_cast<int>(value.size()));
                  });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 6);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just(11) | bexec::let_error([](std::exception_ptr) {
                    return bexec::just(0);
                  });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 11);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just_stopped() |
                  bexec::let_stopped([] { return bexec::just(23); });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 23);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just_error(5) |
                  bexec::let_stopped([] { return bexec::just(0); });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just(1) | bexec::let_value([](int) {
                    throw std::runtime_error("boom");
                    return bexec::just(2);
                  });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
    CHECK(static_cast<bool>(state->exception));
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just(std::make_unique<int>(41)) |
                  bexec::let_value([](std::unique_ptr<int> value) {
                    ++*value;
                    return bexec::just(std::move(value));
                  });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 42);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just(std::make_unique<int>(20), 2) |
                  bexec::let_value([](std::unique_ptr<int> value, int extra) {
                    *value += extra;
                    return bexec::just(std::move(value));
                  });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 22);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = non_movable_value_sender{9} | bexec::let_value([](int value) {
                    return non_movable_value_sender{value + 1};
                  });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 10);
  }

  {
    bexec::run_loop loop;
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just(4) | bexec::let_value([&](int value) {
                    return bexec::schedule(loop.get_scheduler()) |
                           bexec::then([value] { return value + 6; });
                  });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::none);
    loop.finish();
    loop.run();
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 10);
  }
}

}  // namespace bexec_tests
