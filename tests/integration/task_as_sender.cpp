/**
 * @file tests/integration/task_as_sender.cpp
 * @brief Integration tests for task<T> used as a sender.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-08-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * task<T> models the bexec sender concept: it publishes an exact completion
 * signature set and an rvalue-only connect() whose operation state owns the
 * coroutine frame. Compile-time contract probes (sender concept, connect
 * value-category rules, operation-state shape, lvalue co_await rejection)
 * follow the runtime tests.
 */

#include <bexec/associate.hpp>
#include <bexec/awaitable.hpp>
#include <bexec/counting_scope.hpp>
#include <bexec/into_variant.hpp>
#include <bexec/just.hpp>
#include <bexec/let.hpp>
#include <bexec/on.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/receiver.hpp>
#include <bexec/repeat_until.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sender.hpp>
#include <bexec/sync_wait.hpp>
#include <bexec/task.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>
#include <concepts>
#include <coroutine>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

bexec::task<int> value_child(int value) { co_return value; }

bexec::task<void> void_child(bool& ran) {
  ran = true;
  co_return;
}

bexec::task<int> throwing_child() {
  throw std::runtime_error{"child failure"};
  co_return 0;
}

bexec::task<void> stopped_child() { co_await bexec::just_stopped(); }

// Awaits an rvalue child task through the sender bridge and adds to the
// child's value.
bexec::task<int> middle_child(int base) {
  auto child = value_child(base + 1);
  int value = co_await std::move(child);
  co_return value + 1;
}

// Two-level nesting: top -> middle_child -> value_child.
bexec::task<int> top_task(int base) {
  auto child = middle_child(base);
  int value = co_await std::move(child);
  co_return value * 2;
}

// Awaits a void child task and then a value child task, both as rvalues.
bexec::task<int> parent_awaits_children(bool& void_ran, int value) {
  auto first = void_child(void_ran);
  co_await std::move(first);
  auto second = value_child(value);
  int result = co_await std::move(second);
  co_return result + 2;
}

// The child throws; the error must cross the sender bridge and surface from
// the parent task's result().
bexec::task<int> parent_awaits_throwing_child() {
  auto child = throwing_child();
  int value = co_await std::move(child);
  co_return value;
}

}  // namespace

// Scenario 3/8: connect(std::move(task), receiver) + start(op) drives the
// coroutine to completion and delivers set_value with the task result.
TEST(integration, task_as_sender_value_completion) {
  auto child = value_child(42);
  auto state = std::make_shared<shared_state>();
  auto op = bexec::connect(std::move(child), any_receiver{state});

  // Lazy: connecting alone must not run the coroutine.
  EXPECT_EQ(state->signal, signal_kind::none);

  bexec::start(op);
  EXPECT_EQ(state->signal, signal_kind::value);
  EXPECT_EQ(state->int_value, 42);
}

TEST(integration, task_as_sender_void_value_completion) {
  bool ran = false;
  auto child = void_child(ran);
  auto state = std::make_shared<shared_state>();
  auto op = bexec::connect(std::move(child), any_receiver{state});

  EXPECT_EQ(state->signal, signal_kind::none);
  EXPECT_FALSE(ran);

  bexec::start(op);
  EXPECT_EQ(state->signal, signal_kind::value);
  EXPECT_TRUE(ran);
}

// Scenario 4: an exception escaping the coroutine is delivered as
// set_error(std::exception_ptr) preserving the original exception.
TEST(integration, task_as_sender_error_completion) {
  auto child = throwing_child();
  auto state = std::make_shared<shared_state>();
  auto op = bexec::connect(std::move(child), any_receiver{state});

  bexec::start(op);
  ASSERT_EQ(state->signal, signal_kind::error);
  ASSERT_TRUE(state->exception != nullptr);
  try {
    std::rethrow_exception(state->exception);
  } catch (const std::runtime_error& error) {
    EXPECT_STREQ(error.what(), "child failure");
  } catch (...) {
    FAIL() << "unexpected exception type crossing task connect";
  }
}

// Scenario 5: a task that completes stopped (via co_await just_stopped())
// delivers set_stopped to the connected receiver.
TEST(integration, task_as_sender_stopped_completion) {
  auto child = stopped_child();
  auto state = std::make_shared<shared_state>();
  auto op = bexec::connect(std::move(child), any_receiver{state});

  bexec::start(op);
  EXPECT_EQ(state->signal, signal_kind::stopped);
}

// Scenario 6: task awaiting an rvalue task goes through the sender bridge;
// values propagate; two-level nesting works; errors reach result().
TEST(integration, task_as_sender_awaited_via_sender_bridge) {
  bool void_ran = false;
  auto parent = parent_awaits_children(void_ran, 40);
  parent.start();
  ASSERT_TRUE(parent.done());
  EXPECT_TRUE(void_ran);
  EXPECT_EQ(parent.result(), 42);
}

TEST(integration, task_as_sender_nested_await_chain) {
  auto top = top_task(19);  // (19 + 1) + 1 = 21, then 21 * 2 = 42.
  top.start();
  ASSERT_TRUE(top.done());
  EXPECT_EQ(top.result(), 42);
}

TEST(integration, task_as_sender_error_crosses_await_bridge) {
  auto parent = parent_awaits_throwing_child();
  parent.start();
  ASSERT_TRUE(parent.done());

  bool caught = false;
  try {
    (void)parent.result();
  } catch (const std::runtime_error& error) {
    caught = std::string{error.what()} == "child failure";
  }
  EXPECT_TRUE(caught);
}

// --- Scenario 1: task models the sender concept with the expected
// --- completion signatures (exact set, exact order).
static_assert(bexec::sender<bexec::task<int>>);
static_assert(bexec::sender<bexec::task<void>>);
static_assert(
    std::same_as<bexec::completion_signatures_of_t<bexec::task<int>>,
                 bexec::completion_signatures<
                     bexec::set_value_t(int),
                     bexec::set_error_t(std::exception_ptr),
                     bexec::set_stopped_t()>>);
static_assert(
    std::same_as<bexec::completion_signatures_of_t<bexec::task<void>>,
                 bexec::completion_signatures<
                     bexec::set_value_t(),
                     bexec::set_error_t(std::exception_ptr),
                     bexec::set_stopped_t()>>);
static_assert(std::same_as<bexec::value_types_of_t<bexec::task<int>>,
                           std::variant<std::tuple<int>>>);
static_assert(std::same_as<bexec::value_types_of_t<bexec::task<void>>,
                           std::variant<std::tuple<>>>);
static_assert(std::same_as<bexec::error_types_of_t<bexec::task<int>>,
                           std::variant<std::exception_ptr>>);
static_assert(bexec::sends_stopped<bexec::task<int>>);
static_assert(bexec::sends_stopped<bexec::task<void>>);

// --- Scenario 2: connect accepts an rvalue task only, and is noexcept.
template <class Task, class Receiver>
concept task_connect_expression = requires(Task&& task, Receiver&& receiver) {
  bexec::connect(std::forward<Task>(task), std::forward<Receiver>(receiver));
};

template <class Task, class Receiver>
concept task_connect_noexcept = requires(Task&& task, Receiver&& receiver) {
  {
    bexec::connect(std::forward<Task>(task), std::forward<Receiver>(receiver))
  } noexcept;
};

static_assert(task_connect_expression<bexec::task<int>, any_receiver>);
static_assert(task_connect_expression<bexec::task<void>, any_receiver>);
static_assert(!task_connect_expression<bexec::task<int>&, any_receiver>);
static_assert(!task_connect_expression<const bexec::task<int>&, any_receiver>);
static_assert(!task_connect_expression<bexec::task<void>&, any_receiver>);
static_assert(task_connect_noexcept<bexec::task<int>, any_receiver>);
static_assert(task_connect_noexcept<bexec::task<void>, any_receiver>);

// --- Scenario 8: the connect result is an operation state (lvalue, noexcept
// --- start).
using task_int_operation = decltype(bexec::connect(
    std::declval<bexec::task<int>>(), std::declval<any_receiver>()));
using task_void_operation = decltype(bexec::connect(
    std::declval<bexec::task<void>>(), std::declval<any_receiver>()));
static_assert(bexec::operation_state<task_int_operation>);
static_assert(bexec::operation_state<task_void_operation>);

// --- Scenario 7: co_await of an lvalue task is rejected. The probe mirrors
// --- what with_awaitable_senders::await_transform + co_await require: the
// --- await_transform result must be an awaiter for the parent promise. It
// --- must evaluate to false via constraint failure, NOT via a hard error
// --- from instantiating sender_awaitable with an lvalue task (the
// --- implementation has to keep the lvalue rejection SFINAE-friendly, e.g.
// --- by gating the sender branch of as_awaitable on connectability).
using await_probe_promise = bexec::task<void>::promise_type;

template <class Awaiter>
concept awaiter_for_task_promise =
    requires(Awaiter&& awaiter,
             std::coroutine_handle<await_probe_promise> handle) {
      { awaiter.await_ready() } -> std::convertible_to<bool>;
      awaiter.await_suspend(handle);
      awaiter.await_resume();
    };

template <class Value>
concept co_awaitable_in_task =
    awaiter_for_task_promise<decltype(bexec::as_awaitable(
        std::declval<Value>(), std::declval<await_probe_promise&>()))>;

static_assert(co_awaitable_in_task<bexec::task<int>&&>);
static_assert(co_awaitable_in_task<bexec::task<void>&&>);
static_assert(!co_awaitable_in_task<bexec::task<int>&>);
static_assert(!co_awaitable_in_task<const bexec::task<int>&>);
static_assert(!co_awaitable_in_task<bexec::task<void>&>);

// === Rvalue task flows through the sender adaptors =====================
// Each adaptor stores the child sender and connects it later; the stored
// child must reach bexec::connect as an rvalue for task's rvalue-only
// connect to accept it. These tests exercise that full path at runtime.

TEST(integration, task_flows_through_then) {
  auto result = bexec::this_thread::sync_wait(
      bexec::then(value_child(41), [](int value) { return value + 1; }));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(integration, task_error_flows_through_then) {
  auto sender =
      bexec::then(throwing_child(), [](int value) { return value; });
  EXPECT_THROW((void)bexec::this_thread::sync_wait(std::move(sender)),
               std::runtime_error);
}

TEST(integration, task_flows_through_let_value) {
  auto result = bexec::this_thread::sync_wait(bexec::let_value(
      value_child(41), [](int value) { return bexec::just(value + 1); }));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 42);
}

// The let child sender is produced as a prvalue and connected in place; a
// task returned from the callable keeps its rvalue category there.
TEST(integration, task_flows_through_let_value_returning_task) {
  auto result = bexec::this_thread::sync_wait(bexec::let_value(
      value_child(40), [](int value) { return value_child(value + 2); }));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(integration, task_flows_through_into_variant) {
  auto result =
      bexec::this_thread::sync_wait(bexec::into_variant(value_child(42)));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(std::get<0>(std::get<0>(*result))), 42);
}

TEST(integration, task_flows_through_when_all) {
  auto result = bexec::this_thread::sync_wait(
      bexec::when_all(value_child(40), value_child(2)));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 40);
  EXPECT_EQ(std::get<1>(*result), 2);
}

TEST(integration, task_flows_through_when_all_mixed_with_just) {
  auto result = bexec::this_thread::sync_wait(
      bexec::when_all(value_child(40), bexec::just(2)));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 40);
  EXPECT_EQ(std::get<1>(*result), 2);
}

TEST(integration, task_error_flows_through_when_all) {
  auto sender = bexec::when_all(throwing_child(), bexec::just(1));
  EXPECT_THROW((void)bexec::this_thread::sync_wait(std::move(sender)),
               std::runtime_error);
}

TEST(integration, task_stopped_flows_through_when_all) {
  auto result = bexec::this_thread::sync_wait(
      bexec::when_all(stopped_child(), bexec::just(1)));
  EXPECT_FALSE(result.has_value());
}

TEST(integration, task_flows_through_sync_wait) {
  auto result = bexec::this_thread::sync_wait(value_child(42));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 42);

  auto stopped = bexec::this_thread::sync_wait(stopped_child());
  EXPECT_FALSE(stopped.has_value());

  EXPECT_THROW((void)bexec::this_thread::sync_wait(throwing_child()),
               std::runtime_error);
}

TEST(integration, task_flows_through_sync_wait_with_variant) {
  auto result = bexec::this_thread::sync_wait_with_variant(value_child(42));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(std::get<0>(*result)), 42);
}

TEST(integration, task_flows_through_spawn_future) {
  bexec::simple_counting_scope scope;
  auto future = bexec::spawn_future(value_child(42), scope.get_token());
  auto result = bexec::this_thread::sync_wait(std::move(future));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 42);

  scope.close();
  auto joined = bexec::this_thread::sync_wait(scope.join());
  EXPECT_TRUE(joined.has_value());
}

// counting_scope::token::wrap stores the task in a scope_stop_sender; the
// wrapped child must still reach connect as an rvalue.
TEST(integration, task_flows_through_spawn_future_counting_scope) {
  bexec::counting_scope scope;
  auto future = bexec::spawn_future(value_child(42), scope.get_token());
  auto result = bexec::this_thread::sync_wait(std::move(future));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 42);

  scope.close();
  auto joined = bexec::this_thread::sync_wait(scope.join());
  EXPECT_TRUE(joined.has_value());
}

TEST(integration, task_flows_through_associate) {
  bexec::simple_counting_scope scope;
  auto sender = bexec::associate(value_child(42), scope.get_token());
  auto result = bexec::this_thread::sync_wait(std::move(sender));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 42);

  scope.close();
  auto joined = bexec::this_thread::sync_wait(scope.join());
  EXPECT_TRUE(joined.has_value());
}

// repeat_until connects each factory-produced child as an rvalue.
TEST(integration, task_flows_through_repeat_until) {
  int attempts = 0;
  auto result = bexec::this_thread::sync_wait(bexec::repeat_until(
      [&attempts] {
        ++attempts;
        return value_child(42);
      },
      [&attempts] { return attempts >= 3; }));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 42);
  EXPECT_EQ(attempts, 3);
}

TEST(integration, task_flows_through_starts_on) {
  bexec::run_loop loop;
  std::thread worker([&loop] { loop.run(); });
  auto result = bexec::this_thread::sync_wait(
      bexec::starts_on(loop.get_scheduler(), value_child(42)));
  loop.finish();
  worker.join();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(integration, task_flows_through_on) {
  bexec::run_loop loop;
  std::thread worker([&loop] { loop.run(); });
  auto result = bexec::this_thread::sync_wait(
      bexec::on(loop.get_scheduler(), value_child(42)));
  loop.finish();
  worker.join();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::get<0>(*result), 42);
}

// === Value-category probes at the adaptor entry points =================
// An rvalue task is accepted everywhere it is semantically meaningful; an
// lvalue task must be refused by constraint failure (SFINAE), never by a
// hard error inside the implementation.

template <class Task>
concept then_accepts_task = requires(Task&& task) {
  bexec::then(std::forward<Task>(task), [](int) {});
};
static_assert(then_accepts_task<bexec::task<int>>);
static_assert(!then_accepts_task<bexec::task<int>&>);
static_assert(!then_accepts_task<const bexec::task<int>&>);

template <class Task>
concept then_pipe_accepts_task = requires(Task&& task) {
  std::forward<Task>(task) | bexec::then([](int) {});
};
static_assert(then_pipe_accepts_task<bexec::task<int>>);
static_assert(!then_pipe_accepts_task<bexec::task<int>&>);

template <class Task>
concept let_value_accepts_task = requires(Task&& task) {
  bexec::let_value(std::forward<Task>(task),
                   [](int) { return bexec::just(0); });
};
static_assert(let_value_accepts_task<bexec::task<int>>);
static_assert(!let_value_accepts_task<bexec::task<int>&>);

template <class Task>
concept into_variant_accepts_task = requires(Task&& task) {
  bexec::into_variant(std::forward<Task>(task));
};
static_assert(into_variant_accepts_task<bexec::task<int>>);
static_assert(!into_variant_accepts_task<bexec::task<int>&>);

template <class... Tasks>
concept when_all_accepts_tasks = requires(Tasks&&... tasks) {
  bexec::when_all(std::forward<Tasks>(tasks)...);
};
static_assert(when_all_accepts_tasks<bexec::task<int>>);
static_assert(when_all_accepts_tasks<bexec::task<int>, bexec::task<void>>);
static_assert(!when_all_accepts_tasks<bexec::task<int>&>);
static_assert(!when_all_accepts_tasks<bexec::task<int>, bexec::task<int>&>);

template <class... Tasks>
concept when_all_with_variant_accepts_tasks = requires(Tasks&&... tasks) {
  bexec::when_all_with_variant(std::forward<Tasks>(tasks)...);
};
static_assert(when_all_with_variant_accepts_tasks<bexec::task<int>>);
static_assert(!when_all_with_variant_accepts_tasks<bexec::task<int>&>);

template <class Task>
concept starts_on_accepts_task =
    requires(Task&& task, bexec::run_loop::scheduler scheduler) {
      bexec::starts_on(scheduler, std::forward<Task>(task));
    };
static_assert(starts_on_accepts_task<bexec::task<int>>);
static_assert(!starts_on_accepts_task<bexec::task<int>&>);

template <class Task>
concept on_accepts_task =
    requires(Task&& task, bexec::run_loop::scheduler scheduler) {
      bexec::on(scheduler, std::forward<Task>(task));
    };
static_assert(on_accepts_task<bexec::task<int>>);
static_assert(!on_accepts_task<bexec::task<int>&>);

template <class Sender>
concept sync_wait_accepts = requires(Sender&& sender) {
  bexec::this_thread::sync_wait(std::forward<Sender>(sender));
};
static_assert(sync_wait_accepts<bexec::task<int>>);
static_assert(sync_wait_accepts<bexec::task<void>>);
static_assert(!sync_wait_accepts<bexec::task<int>&>);
static_assert(!sync_wait_accepts<bexec::task<void>&>);

template <class Sender>
concept sync_wait_with_variant_accepts = requires(Sender&& sender) {
  bexec::this_thread::sync_wait_with_variant(std::forward<Sender>(sender));
};
static_assert(sync_wait_with_variant_accepts<bexec::task<int>>);
static_assert(!sync_wait_with_variant_accepts<bexec::task<int>&>);

template <class Task>
concept spawn_future_accepts_task =
    requires(Task&& task, bexec::simple_counting_scope::token token) {
      bexec::spawn_future(std::forward<Task>(task), token);
    };
static_assert(spawn_future_accepts_task<bexec::task<int>>);
static_assert(spawn_future_accepts_task<bexec::task<void>>);
static_assert(!spawn_future_accepts_task<bexec::task<int>&>);

// spawn rejects any sender with an error channel (task always declares
// set_error(std::exception_ptr)), for both value categories.
template <class Task>
concept spawn_accepts_task =
    requires(Task&& task, bexec::simple_counting_scope::token token) {
      bexec::spawn(std::forward<Task>(task), token);
    };
static_assert(!spawn_accepts_task<bexec::task<void>>);
static_assert(!spawn_accepts_task<bexec::task<void>&>);

template <class Task>
concept associate_accepts_task =
    requires(Task&& task, bexec::simple_counting_scope::token token) {
      bexec::associate(std::forward<Task>(task), token);
    };
static_assert(associate_accepts_task<bexec::task<int>>);
static_assert(!associate_accepts_task<bexec::task<int>&>);

// repeat_until produces each child from the factory as a prvalue, so a
// task-returning factory is accepted.
static_assert(requires {
  bexec::repeat_until([] { return bexec::task<int>{}; },
                      [] { return true; });
});

// The adapted senders stay connectable to a generic receiver: this
// instantiates every adaptor's child-connect expression with an rvalue
// task at the type level.
static_assert(
    bexec::sender_to<decltype(bexec::then(std::declval<bexec::task<int>>(),
                                          [](int) {})),
                     any_receiver>);
static_assert(
    bexec::sender_to<decltype(bexec::let_value(
                         std::declval<bexec::task<int>>(),
                         [](int) { return bexec::just(0); })),
                     any_receiver>);
static_assert(
    bexec::sender_to<decltype(bexec::into_variant(
                         std::declval<bexec::task<int>>())),
                     any_receiver>);
static_assert(
    bexec::sender_to<decltype(bexec::when_all(
                         std::declval<bexec::task<int>>(),
                         std::declval<bexec::task<void>>())),
                     any_receiver>);

// A task child carries its own set_error(std::exception_ptr) signature, so
// when_all's extracted error set keeps exception_ptr regardless of the
// noexcept extraction, and the child-connect guard (when_all.hpp:129-156)
// is never on the rejecting path.
static_assert(std::same_as<
              bexec::error_types_of_t<decltype(bexec::when_all(
                  std::declval<bexec::task<int>>(),
                  std::declval<bexec::task<int>>()))>,
              std::variant<std::exception_ptr>>);
static_assert(std::same_as<
              bexec::value_types_of_t<decltype(bexec::when_all(
                  std::declval<bexec::task<int>>(),
                  std::declval<bexec::task<int>>()))>,
              std::variant<std::tuple<int, int>>>);

}  // namespace bexec_tests
