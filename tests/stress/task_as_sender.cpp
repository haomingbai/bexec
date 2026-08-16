/**
 * @file tests/stress/task_as_sender.cpp
 * @brief Stress tests for task<T> used as a sender.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-08-16
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Loops the task-as-sender lifecycle: connect/start/destroy churn, nested
 * co_await bridges, error and stopped paths, cross-thread completion through
 * run_loop (starts_on/on), multi-producer scheduling, concurrent when_all
 * completion of task children, and spawn_future reclamation. Sanitizer
 * builds (ASAN/TSAN) over these cases validate frame reclamation and the
 * thread-safety of dispatch_connected_completion.
 */

#include <atomic>
#include <bexec/completion_signatures.hpp>
#include <bexec/counting_scope.hpp>
#include <bexec/just.hpp>
#include <bexec/on.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sync_wait.hpp>
#include <bexec/task.hpp>
#include <bexec/when_all.hpp>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

bexec::task<int> sender_indexed_task(int value) { co_return value; }

bexec::task<void> sender_void_task() { co_return; }

bexec::task<int> sender_throwing_task() {
  throw std::runtime_error{"stress child failure"};
  co_return 0;
}

bexec::task<void> sender_stopped_task() { co_await bexec::just_stopped(); }

// Three-level nesting: level 2 -> level 1 -> leaf, all through the rvalue
// sender bridge.
bexec::task<int> sender_nested_leaf(int value) { co_return value; }

bexec::task<int> sender_nested_level1(int value) {
  auto child = sender_nested_leaf(value + 1);
  int result = co_await std::move(child);
  co_return result + 1;
}

bexec::task<int> sender_nested_level2(int value) {
  auto child = sender_nested_level1(value);
  int result = co_await std::move(child);
  co_return result * 2;
}

// Parent resumed on whatever thread the awaited sender completes on.
bexec::task<int> sender_parent_awaiting_child(int value) {
  auto child = sender_indexed_task(value + 1);
  int result = co_await std::move(child);
  co_return result + 1;
}

// Parent awaiting a stopped child: the stopped signal crosses the sender
// bridge and parks the parent at its await point.
bexec::task<int> sender_parent_awaiting_child_stopped() {
  auto child = sender_stopped_task();
  co_await std::move(child);
  co_return 0;
}

// Parent awaiting a scheduled stopped child: the child completes stopped on
// the loop's worker thread and the parent's stopped completion is dispatched
// from there while the driving thread reclaims the parent's frame.
bexec::task<int> sender_parent_cross_thread_stopped(
    bexec::run_loop::scheduler scheduler) {
  co_await bexec::starts_on(scheduler, sender_stopped_task());
  co_return 0;
}

// Parent awaiting a scheduled throwing child: the exception crosses the
// await bridge on the worker thread and the parent completes with error
// there.
bexec::task<int> sender_parent_cross_thread_throwing(
    bexec::run_loop::scheduler scheduler) {
  int value = co_await bexec::starts_on(scheduler, sender_throwing_task());
  co_return value;
}

// Sender whose operation never completes: start() is a no-op. Used to park a
// task at its await point so the operation can be destroyed while the
// coroutine is suspended mid-await.
struct never_sender {
  template <class Receiver>
  struct operation {
    Receiver receiver;
    void start() noexcept {}
  };

  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t()>;

  template <class Receiver>
  operation<Receiver> connect(Receiver&& receiver) {
    return operation<Receiver>{std::forward<Receiver>(receiver)};
  }
};

bexec::task<void> sender_parked_task() { co_await never_sender{}; }

// Parent awaiting a child that is itself scheduled onto a run_loop: the
// parent is resumed on the loop's worker thread.
bexec::task<int> sender_parent_cross_thread(bexec::run_loop::scheduler scheduler,
                                            int value) {
  int result = co_await bexec::starts_on(scheduler, sender_indexed_task(value));
  co_return result + 1;
}

// Two-level cross-thread nesting: the middle task is resumed on the worker
// thread and the leaf completes there as well.
bexec::task<int> sender_middle_cross_thread(bexec::run_loop::scheduler scheduler,
                                            int value) {
  int result = co_await bexec::starts_on(scheduler, sender_indexed_task(value));
  co_return result * 2;
}

bexec::task<int> sender_top_cross_thread(bexec::run_loop::scheduler scheduler,
                                         int value) {
  auto middle = sender_middle_cross_thread(scheduler, value + 1);
  int result = co_await std::move(middle);
  co_return result + 1;
}

struct atomic_count_receiver {
  std::atomic<int>* count{};
  std::atomic<long long>* sum{};

  void set_value(int value) noexcept {
    count->fetch_add(1, std::memory_order_relaxed);
    sum->fetch_add(value, std::memory_order_relaxed);
  }
  template <class Error>
  void set_error(Error&&) noexcept {}
  void set_stopped() noexcept {}
};

}  // namespace

// connect + start + destroy churn through the sender interface: the
// operation owns the frame, final_suspend dispatches into the receiver, and
// the frame is reclaimed when the operation goes out of scope.
TEST(stress, task_as_sender_connect_start_destroy_loop) {
  const int iterations = stress_iterations(50000);
  for (int index = 0; index != iterations; ++index) {
    auto state = std::make_shared<shared_state>();
    auto operation =
        bexec::connect(sender_indexed_task(index), any_receiver{state});
    bexec::start(operation);
    ASSERT_EQ(state->signal, signal_kind::value);
    ASSERT_EQ(state->int_value, index);
  }
}

// A connected operation destroyed before start() must reclaim the coroutine
// frame parked at initial_suspend.
TEST(stress, task_as_sender_destroy_without_start_loop) {
  const int iterations = stress_iterations(50000);
  for (int index = 0; index != iterations; ++index) {
    auto state = std::make_shared<shared_state>();
    {
      auto operation =
          bexec::connect(sender_indexed_task(index), any_receiver{state});
      EXPECT_EQ(state->signal, signal_kind::none);
    }
    EXPECT_EQ(state->signal, signal_kind::none);
  }
}

// A task suspended at an await point (its sender never completes) is
// reclaimed with the live sender_awaitable and its nested operation state
// when the owning operation is destroyed.
TEST(stress, task_as_sender_destroy_suspended_await_loop) {
  const int iterations = stress_iterations(20000);
  for (int index = 0; index != iterations; ++index) {
    auto state = std::make_shared<shared_state>();
    {
      auto operation = bexec::connect(sender_parked_task(), any_receiver{state});
      bexec::start(operation);
      EXPECT_EQ(state->signal, signal_kind::none);
    }
    EXPECT_EQ(state->signal, signal_kind::none);
  }
}

// Multi-level rvalue co_await chains driven through sync_wait, which also
// exercises the top-level connect path on every iteration.
TEST(stress, task_as_sender_nested_await_loop) {
  const int iterations = stress_iterations(20000);
  for (int index = 0; index != iterations; ++index) {
    auto result =
        bexec::this_thread::sync_wait(sender_nested_level2(index));
    ASSERT_TRUE(result.has_value());
    // ((index + 1) + 1) * 2
    EXPECT_EQ(std::get<0>(*result), (index + 2) * 2);
  }
}

// Exceptions escaping a task cross connect as set_error(std::exception_ptr)
// and cross the await bridge unchanged, iteration after iteration.
TEST(stress, task_as_sender_error_loop) {
  const int iterations = stress_iterations(20000);
  for (int index = 0; index != iterations; ++index) {
    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(sender_throwing_task(), any_receiver{state});
    bexec::start(operation);
    ASSERT_EQ(state->signal, signal_kind::error);
    ASSERT_TRUE(state->exception != nullptr);
    try {
      std::rethrow_exception(state->exception);
    } catch (const std::runtime_error& error) {
      EXPECT_STREQ(error.what(), "stress child failure");
    } catch (...) {
      FAIL() << "unexpected exception type crossing task connect";
    }
  }
}

// A stopped task delivers set_stopped through connect; a parent awaiting a
// stopped child reports stopped from result() and through sync_wait.
TEST(stress, task_as_sender_stopped_loop) {
  const int iterations = stress_iterations(20000);
  for (int index = 0; index != iterations; ++index) {
    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(sender_stopped_task(), any_receiver{state});
    bexec::start(operation);
    ASSERT_EQ(state->signal, signal_kind::stopped);
  }

  for (int index = 0; index != iterations; ++index) {
    auto result = bexec::this_thread::sync_wait(sender_stopped_task());
    EXPECT_FALSE(result.has_value());
  }

  for (int index = 0; index != iterations; ++index) {
    auto parent = sender_parent_awaiting_child_stopped();
    parent.start();
    ASSERT_TRUE(parent.done());
    EXPECT_THROW((void)parent.result(), bexec::task_stopped);
  }
}

// Tasks completed on a worker-thread run_loop via starts_on, driven by
// sync_wait from the main thread.
TEST(stress, task_as_sender_cross_thread_starts_on_loop) {
  bexec::run_loop loop;
  std::thread worker([&] { loop.run(); });

  const int iterations = stress_iterations(2000);
  for (int index = 0; index != iterations; ++index) {
    auto result = bexec::this_thread::sync_wait(
        bexec::starts_on(loop.get_scheduler(), sender_indexed_task(index)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), index);
  }

  loop.finish();
  worker.join();
}

// Tasks completed via on(): the value hops from the worker loop back onto
// sync_wait's own loop before delivery.
TEST(stress, task_as_sender_cross_thread_on_loop) {
  bexec::run_loop loop;
  std::thread worker([&] { loop.run(); });

  const int iterations = stress_iterations(2000);
  for (int index = 0; index != iterations; ++index) {
    auto result = bexec::this_thread::sync_wait(
        bexec::on(loop.get_scheduler(), sender_indexed_task(index)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), index);
  }

  loop.finish();
  worker.join();
}

// Error and stopped completions dispatched on the worker thread: sync_wait
// reclaims the operation (and the suspended coroutine frame) on the driving
// thread as the worker unwinds the delivery chain.
TEST(stress, task_as_sender_cross_thread_error_stopped_loop) {
  bexec::run_loop loop;
  std::thread worker([&] { loop.run(); });

  const int iterations = stress_iterations(2000);
  for (int index = 0; index != iterations; ++index) {
    auto error_sender =
        bexec::starts_on(loop.get_scheduler(), sender_throwing_task());
    EXPECT_THROW((void)bexec::this_thread::sync_wait(std::move(error_sender)),
                 std::runtime_error);

    auto stopped_result = bexec::this_thread::sync_wait(
        bexec::starts_on(loop.get_scheduler(), sender_stopped_task()));
    EXPECT_FALSE(stopped_result.has_value());

    auto parent_stopped = bexec::this_thread::sync_wait(
        sender_parent_cross_thread_stopped(loop.get_scheduler()));
    EXPECT_FALSE(parent_stopped.has_value());

    EXPECT_THROW(
        (void)bexec::this_thread::sync_wait(
            sender_parent_cross_thread_throwing(loop.get_scheduler())),
        std::runtime_error);

    auto on_error_sender = bexec::on(loop.get_scheduler(), sender_throwing_task());
    EXPECT_THROW((void)bexec::this_thread::sync_wait(std::move(on_error_sender)),
                 std::runtime_error);

    auto on_stopped = bexec::this_thread::sync_wait(
        bexec::on(loop.get_scheduler(), sender_stopped_task()));
    EXPECT_FALSE(on_stopped.has_value());
  }

  loop.finish();
  worker.join();
}

// A parent task awaiting starts_on(scheduler, child) is itself resumed on
// the worker thread; nested variants resume both levels off-thread.
TEST(stress, task_as_sender_await_cross_thread_resume_loop) {
  bexec::run_loop loop;
  std::thread worker([&] { loop.run(); });

  const int iterations = stress_iterations(2000);
  for (int index = 0; index != iterations; ++index) {
    auto result = bexec::this_thread::sync_wait(
        sender_parent_cross_thread(loop.get_scheduler(), index));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), index + 1);
  }

  for (int index = 0; index != iterations; ++index) {
    auto result = bexec::this_thread::sync_wait(
        sender_top_cross_thread(loop.get_scheduler(), index));
    ASSERT_TRUE(result.has_value());
    // (index + 1) * 2 + 1
    EXPECT_EQ(std::get<0>(*result), (index + 1) * 2 + 1);
  }

  loop.finish();
  worker.join();
}

// Many producer threads enqueue starts_on(scheduler, task) operations onto a
// single run_loop; every task completes on the worker thread and reports
// back to a shared atomic receiver.
TEST(stress, task_as_sender_multi_producer_starts_on) {
  bexec::run_loop loop;
  std::atomic<int> completed{0};
  std::atomic<long long> sum{0};
  auto scheduler = loop.get_scheduler();
  using sender_type = decltype(bexec::starts_on(scheduler, sender_indexed_task(0)));
  using operation_type = decltype(bexec::connect(
      std::declval<sender_type>(), atomic_count_receiver{&completed, &sum}));

  const int thread_count = 4;
  const int operations_per_thread = stress_iterations(500);
  std::mutex operations_mutex;
  std::vector<std::unique_ptr<operation_type>> operations;
  operations.reserve(
      static_cast<std::size_t>(thread_count * operations_per_thread));

  std::thread runner([&] { loop.run(); });
  std::vector<std::thread> producers;
  for (int thread_index = 0; thread_index != thread_count; ++thread_index) {
    producers.emplace_back([&, thread_index] {
      for (int index = 0; index != operations_per_thread; ++index) {
        const int value = thread_index * operations_per_thread + index;
        auto operation = std::make_unique<operation_type>(
            scheduler, sender_indexed_task(value),
            atomic_count_receiver{&completed, &sum});
        operation_type* raw_operation = operation.get();
        {
          std::lock_guard lock(operations_mutex);
          operations.push_back(std::move(operation));
        }
        bexec::start(*raw_operation);
      }
    });
  }

  for (auto& producer : producers) {
    producer.join();
  }
  loop.finish();
  runner.join();

  const long long expected_sum =
      static_cast<long long>(thread_count * operations_per_thread - 1) *
      (thread_count * operations_per_thread) / 2;
  EXPECT_EQ(completed.load(std::memory_order_relaxed),
            thread_count * operations_per_thread);
  EXPECT_EQ(sum.load(std::memory_order_relaxed), expected_sum);
}

// when_all over task children scheduled onto two different worker threads:
// dispatch_connected_completion fires concurrently from both threads into
// the shared when_all state.
TEST(stress, task_as_sender_when_all_concurrent_children) {
  bexec::run_loop loop_a;
  bexec::run_loop loop_b;
  std::thread worker_a([&] { loop_a.run(); });
  std::thread worker_b([&] { loop_b.run(); });

  const int iterations = stress_iterations(1000);
  for (int index = 0; index != iterations; ++index) {
    auto result = bexec::this_thread::sync_wait(bexec::when_all(
        bexec::starts_on(loop_a.get_scheduler(), sender_indexed_task(index)),
        bexec::starts_on(loop_b.get_scheduler(), sender_indexed_task(index + 1)),
        bexec::starts_on(loop_a.get_scheduler(), sender_void_task())));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), index);
    EXPECT_EQ(std::get<1>(*result), index + 1);
  }

  loop_a.finish();
  loop_b.finish();
  worker_a.join();
  worker_b.join();
}

// Terminal-kind races: an erroring task on one thread against a value task
// on the other, and a stopped task against a value task. The when_all
// outcome must be stable regardless of arrival order.
TEST(stress, task_as_sender_when_all_terminal_race) {
  bexec::run_loop loop_a;
  bexec::run_loop loop_b;
  std::thread worker_a([&] { loop_a.run(); });
  std::thread worker_b([&] { loop_b.run(); });

  const int iterations = stress_iterations(1000);
  for (int index = 0; index != iterations; ++index) {
    auto error_sender = bexec::when_all(
        bexec::starts_on(loop_a.get_scheduler(), sender_throwing_task()),
        bexec::starts_on(loop_b.get_scheduler(), sender_indexed_task(index)));
    EXPECT_THROW((void)bexec::this_thread::sync_wait(std::move(error_sender)),
                 std::runtime_error);

    auto stopped_sender = bexec::when_all(
        bexec::starts_on(loop_a.get_scheduler(), sender_stopped_task()),
        bexec::starts_on(loop_b.get_scheduler(), sender_indexed_task(index)));
    auto stopped_result =
        bexec::this_thread::sync_wait(std::move(stopped_sender));
    EXPECT_FALSE(stopped_result.has_value());
  }

  loop_a.finish();
  loop_b.finish();
  worker_a.join();
  worker_b.join();
}

// spawn_future over tasks in a counting_scope, batching futures and joining
// the scope each round: heap state and coroutine frames must all be
// reclaimed.
TEST(stress, task_as_sender_spawn_future_scope_loop) {
  const int iterations = stress_iterations(500);
  for (int index = 0; index != iterations; ++index) {
    bexec::simple_counting_scope scope;
    std::vector<decltype(bexec::spawn_future(sender_indexed_task(0),
                                             scope.get_token()))>
        futures;
    for (int batch = 0; batch != 8; ++batch) {
      futures.push_back(bexec::spawn_future(sender_indexed_task(index + batch),
                                            scope.get_token()));
    }
    for (int batch = 0; batch != 8; ++batch) {
      auto result =
          bexec::this_thread::sync_wait(std::move(futures[batch]));
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(std::get<0>(*result), index + batch);
    }
    scope.close();
    auto joined = bexec::this_thread::sync_wait(scope.join());
    EXPECT_TRUE(joined.has_value());
  }
}

}  // namespace bexec_tests
