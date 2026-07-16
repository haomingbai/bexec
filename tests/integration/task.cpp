/**
 * @file tests/integration/task.cpp
 * @brief Coroutine task movement, exception, and suspension tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/receiver.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/task.hpp>
#include <coroutine>
#include <exception>
#include <stdexcept>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

bexec::task<int> throwing_task() {
  throw std::runtime_error{"task failure"};
  co_return 0;
}

bexec::task<int> indexed_task(int value) { co_return value; }

bexec::task<int> twice_suspended_task(int& progress) {
  ++progress;
  co_await std::suspend_always{};
  ++progress;
  co_return 42;
}

bexec::task<int> scheduled_task(bexec::run_loop& loop) {
  co_await bexec::schedule(loop.get_scheduler());
  co_return 42;
}

bexec::task<int> scheduled_child_task(bexec::run_loop& loop, int& progress) {
  ++progress;
  co_await bexec::schedule(loop.get_scheduler());
  ++progress;
  co_return 41;
}

bexec::task<int> scheduled_parent_task(bexec::run_loop& loop, int& progress) {
  ++progress;
  int value = co_await scheduled_child_task(loop, progress);
  ++progress;
  co_return value + 1;
}

bexec::task<int> child_value_task() { co_return 40; }

bexec::task<void> child_void_task(bool& ran) {
  ran = true;
  co_return;
}

bexec::task<int> parent_task(bool& ran) {
  co_await child_void_task(ran);
  co_return (co_await child_value_task()) + 2;
}

bexec::task<void> stopped_task(bool& continued) {
  co_await bexec::just_stopped();
  continued = true;
}

bexec::task<void> parent_stopped_task(bool& continued) {
  co_await stopped_task(continued);
  continued = true;
}

bexec::task<void> stopped_inside_catch(bool& caught, bool& continued) {
  try {
    co_await bexec::just_stopped();
  } catch (...) {
    caught = true;
  }
  continued = true;
}

bexec::task<void> await_stopped_child(bexec::task<void> child, bool& caught,
                                      bool& continued) {
  try {
    co_await std::move(child);
  } catch (...) {
    caught = true;
  }
  continued = true;
}

bexec::task<int> sender_error_task() {
  co_await bexec::just_error(std::runtime_error{"sender failure"});
  co_return 0;
}

class immovable_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(int)>;

  template <class Receiver>
  class operation {
   public:
    explicit operation(Receiver receiver) : receiver_(std::move(receiver)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    void start() noexcept { bexec::set_value(std::move(receiver_), 17); }

   private:
    Receiver receiver_;
  };

  template <class Receiver>
  operation<Receiver> connect(Receiver receiver) const {
    return operation<Receiver>{std::move(receiver)};
  }
};

bexec::task<int> immovable_operation_task() {
  co_return co_await immovable_sender{};
}

struct probe_env {
  bool* observed;
};

class environment_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t()>;

  template <class Receiver>
  class operation {
   public:
    explicit operation(Receiver receiver) : receiver_(std::move(receiver)) {}

    void start() noexcept {
      auto env = bexec::get_env(receiver_);
      *env.observed = true;
      bexec::set_value(std::move(receiver_));
    }

   private:
    Receiver receiver_;
  };

  template <class Receiver>
  operation<Receiver> connect(Receiver receiver) const {
    return operation<Receiver>{std::move(receiver)};
  }
};

class environment_task {
 public:
  struct promise_type : bexec::with_awaitable_senders<promise_type> {
    explicit promise_type(bool& observed) noexcept : env_{&observed} {}

    environment_task get_return_object() noexcept {
      return environment_task{handle_type::from_promise(*this)};
    }

    std::suspend_always initial_suspend() const noexcept { return {}; }
    std::suspend_always final_suspend() const noexcept { return {}; }
    void return_void() const noexcept {}
    void unhandled_exception() noexcept { error_ = std::current_exception(); }

    [[nodiscard]] probe_env get_env() const noexcept { return env_; }
    probe_env env_;
    std::exception_ptr error_;
  };

  using handle_type = std::coroutine_handle<promise_type>;

  explicit environment_task(handle_type handle) noexcept : handle_(handle) {}
  environment_task(const environment_task&) = delete;
  environment_task& operator=(const environment_task&) = delete;

  ~environment_task() {
    if (handle_) {
      handle_.destroy();
    }
  }

  void start() { handle_.resume(); }
  [[nodiscard]] bool done() const noexcept { return handle_.done(); }

 private:
  handle_type handle_;
};

environment_task observe_promise_environment(bool& observed) {
  (void)observed;
  co_await environment_sender{};
}

}  // namespace

TEST(integration, task_move_and_exception_propagation) {
  auto original = indexed_task(42);
  auto moved = std::move(original);
  EXPECT_TRUE(original.done());
  EXPECT_TRUE(!moved.done());
  moved.start();
  EXPECT_TRUE(moved.done());
  EXPECT_TRUE(moved.result() == 42);

  auto failure = throwing_task();
  failure.start();
  bool caught = false;
  try {
    (void)failure.result();
  } catch (const std::runtime_error&) {
    caught = true;
  }
  EXPECT_TRUE(caught);

  int progress = 0;
  auto suspended = twice_suspended_task(progress);
  suspended.start();
  EXPECT_TRUE(!suspended.done());
  EXPECT_TRUE(progress == 1);
  suspended.start();
  EXPECT_TRUE(suspended.done());
  EXPECT_TRUE(progress == 2);
  EXPECT_TRUE(suspended.result() == 42);
}

TEST(integration, task_awaits_async_sender_and_child_tasks) {
  bexec::run_loop loop;
  auto scheduled = scheduled_task(loop);

  scheduled.start();
  EXPECT_TRUE(!scheduled.done());

  loop.finish();
  loop.run();
  EXPECT_TRUE(scheduled.done());
  EXPECT_TRUE(scheduled.result() == 42);

  bool child_ran = false;
  auto parent = parent_task(child_ran);
  parent.start();
  EXPECT_TRUE(parent.done());
  EXPECT_TRUE(child_ran);
  EXPECT_TRUE(parent.result() == 42);
}

TEST(integration, task_nested_async_continuation_chain) {
  bexec::run_loop loop;
  int progress = 0;
  auto parent = scheduled_parent_task(loop, progress);

  parent.start();
  EXPECT_TRUE(!parent.done());
  EXPECT_TRUE(progress == 2);

  loop.finish();
  loop.run();

  EXPECT_TRUE(parent.done());
  EXPECT_TRUE(progress == 4);
  EXPECT_TRUE(parent.result() == 42);
}

TEST(integration, task_sender_error_and_stopped_propagation) {
  auto failed = sender_error_task();
  failed.start();
  EXPECT_TRUE(failed.done());

  bool caught_error = false;
  try {
    (void)failed.result();
  } catch (const std::runtime_error& error) {
    caught_error = std::string_view{error.what()} == "sender failure";
  }
  EXPECT_TRUE(caught_error);

  bool continued = false;
  auto stopped = parent_stopped_task(continued);
  stopped.start();
  EXPECT_TRUE(stopped.done());
  EXPECT_TRUE(!continued);

  bool caught_stopped = false;
  try {
    stopped.result();
  } catch (const bexec::task_stopped&) {
    caught_stopped = true;
  }
  EXPECT_TRUE(caught_stopped);

  bool caught = false;
  bool catch_continued = false;
  auto inside_catch = stopped_inside_catch(caught, catch_continued);
  inside_catch.start();
  EXPECT_TRUE(inside_catch.done());
  EXPECT_TRUE(!caught);
  EXPECT_TRUE(!catch_continued);

  auto child = stopped_task(catch_continued);
  child.start();
  EXPECT_TRUE(child.done());

  auto await_existing =
      await_stopped_child(std::move(child), caught, catch_continued);
  await_existing.start();
  EXPECT_TRUE(await_existing.done());
  EXPECT_TRUE(!caught);
  EXPECT_TRUE(!catch_continued);
}

TEST(integration, task_supports_immovable_operations_and_promise_env) {
  auto immovable = immovable_operation_task();
  immovable.start();
  EXPECT_TRUE(immovable.done());
  EXPECT_TRUE(immovable.result() == 17);

  bool observed = false;
  auto environment = observe_promise_environment(observed);
  environment.start();
  EXPECT_TRUE(environment.done());
  EXPECT_TRUE(observed);
}

}  // namespace bexec_tests
