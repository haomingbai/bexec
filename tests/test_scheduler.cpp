/**
 * @file tests/test_scheduler.cpp
 * @brief Tests scheduler behavior.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Exercises FIFO queue execution, scheduler equality, scheduled sender
 * completion, and cancellation-aware scheduling.
 */

#include <atomic>
#include <bexec/just.hpp>
#include <bexec/on.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <bexec/sync_wait.hpp>
#include <bexec/then.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

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

struct counting_receiver {
  std::atomic<int>* count{};

  void set_value() noexcept { count->fetch_add(1, std::memory_order_relaxed); }

  template <class Error>
  void set_error(Error&&) noexcept {}

  void set_stopped() noexcept {}
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
    bexec::run_loop loop;
    bool ran = false;
    auto state = std::make_shared<shared_state>();

    auto sender = bexec::schedule(loop.get_scheduler()) |
                  bexec::then([&] { ran = true; });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(!ran);
    CHECK(state->signal == signal_kind::none);
    loop.finish();
    loop.run();
    CHECK(ran);
    CHECK(state->signal == signal_kind::value);
  }

  {
    bexec::run_loop loop;
    std::vector<int> order;
    auto scheduler = loop.get_scheduler();

    auto first =
        bexec::schedule(scheduler) | bexec::then([&] { order.push_back(1); });
    auto second =
        bexec::schedule(scheduler) | bexec::then([&] { order.push_back(2); });
    auto third =
        bexec::schedule(scheduler) | bexec::then([&] { order.push_back(3); });
    auto first_operation = bexec::connect(std::move(first), any_receiver{});
    auto second_operation = bexec::connect(std::move(second), any_receiver{});
    auto third_operation = bexec::connect(std::move(third), any_receiver{});

    bexec::start(first_operation);
    bexec::start(second_operation);
    bexec::start(third_operation);
    loop.finish();
    loop.run();

    CHECK(order.size() == 3);
    if (order.size() == 3) {
      CHECK(order[0] == 1);
      CHECK(order[1] == 2);
      CHECK(order[2] == 3);
    }
  }

  {
    bexec::run_loop loop;
    std::atomic<int> completed{0};
    auto sender = bexec::schedule(loop.get_scheduler());
    using operation_type =
        decltype(bexec::connect(sender, counting_receiver{&completed}));
    constexpr int thread_count = 4;
    constexpr int operations_per_thread = 128;
    constexpr int operation_count = thread_count * operations_per_thread;
    std::mutex operations_mutex;
    std::vector<std::unique_ptr<operation_type>> operations;
    operations.reserve(operation_count);
    std::atomic<int> ready{0};
    std::atomic<bool> released{false};

    std::thread runner([&] { loop.run(); });
    std::vector<std::thread> producers;
    producers.reserve(thread_count);
    for (int thread = 0; thread != thread_count; ++thread) {
      producers.emplace_back([&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!released.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        for (int index = 0; index != operations_per_thread; ++index) {
          auto operation = std::make_unique<operation_type>(
              loop, counting_receiver{&completed});
          operation_type* operation_ptr = operation.get();
          {
            std::lock_guard lock(operations_mutex);
            operations.push_back(std::move(operation));
          }
          bexec::start(*operation_ptr);
        }
      });
    }
    while (ready.load(std::memory_order_acquire) != thread_count) {
      std::this_thread::yield();
    }
    released.store(true, std::memory_order_release);
    for (std::thread& producer : producers) {
      producer.join();
    }
    loop.finish();
    runner.join();

    CHECK(completed.load(std::memory_order_relaxed) == operation_count);
  }

  {
    bexec::run_loop loop;
    std::thread runner([&] { loop.run(); });
    loop.finish();
    runner.join();
  }

  {
    bexec::run_loop loop;
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::starts_on(loop.get_scheduler(), bexec::just(7));
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::none);
    loop.finish();
    loop.run();
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
    target.finish();
    target.run();
    CHECK(child_ran);
    CHECK(state->signal == signal_kind::none);
    final.finish();
    final.run();
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
