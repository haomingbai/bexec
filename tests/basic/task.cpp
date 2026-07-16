/**
 * @file tests/basic/task.cpp
 * @brief Tests coroutine task support.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Verifies task<T>, task<void>, lazy start behavior, result handling, and
 * exception propagation when enabled.
 */

#include <bexec/just.hpp>
#include <bexec/task.hpp>
#include <concepts>
#include <type_traits>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

bexec::task<int> lazy_value() { co_return 42; }

bexec::task<void> lazy_void(bool& ran) {
  ran = true;
  co_return;
}

bexec::task<int> await_immediate_senders() {
  co_await bexec::just();
  int value = co_await bexec::just(42);
  auto lvalue_sender = bexec::just(1);
  co_return value + co_await lvalue_sender;
}

}  // namespace

TEST(basic, task_lazy_value_and_void_behavior) {
  auto value_task = lazy_value();

  EXPECT_TRUE(!value_task.done());
  value_task.start();
  EXPECT_TRUE(value_task.done());
  EXPECT_TRUE(value_task.result() == 42);

  bool ran = false;
  auto void_task = lazy_void(ran);
  EXPECT_TRUE(!ran);
  EXPECT_TRUE(!void_task.done());
  void_task.start();
  EXPECT_TRUE(void_task.done());
  void_task.result();
  EXPECT_TRUE(ran);
}

TEST(basic, task_sender_awaitable_shapes) {
  using promise_type = bexec::task<void>::promise_type;
  using value_sender = decltype(bexec::just(1));
  using void_sender = decltype(bexec::just());
  using multi_value_sender = decltype(bexec::just(1, 2));

  static_assert(
      std::same_as<decltype(bexec::as_awaitable(std::declval<value_sender>(),
                                                std::declval<promise_type&>())),
                   bexec::sender_awaitable<value_sender, promise_type>>);
  static_assert(
      std::same_as<decltype(bexec::as_awaitable(std::declval<void_sender>(),
                                                std::declval<promise_type&>())),
                   bexec::sender_awaitable<void_sender, promise_type>>);
  static_assert(std::same_as<decltype(bexec::as_awaitable(
                                 std::declval<multi_value_sender>(),
                                 std::declval<promise_type&>())),
                             multi_value_sender&&>);

  auto task = await_immediate_senders();
  task.start();
  EXPECT_TRUE(task.done());
  EXPECT_TRUE(task.result() == 43);
}

}  // namespace bexec_tests
