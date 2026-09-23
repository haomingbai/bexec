/**
 * @file benchmarks/task.cpp
 * @brief Benchmarks for task coroutine round trips.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <bexec/task.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexec/coroutine.hpp>
#include <stdexec/execution.hpp>
#include <utility>

#include "bench_support.hpp"

// Both libraries drive a task the same way: connect it to the shared
// receiver and start the operation state. This compares coroutine frame
// handling, promise machinery, and sender awaiting directly.

namespace {

using bexec_bench::bench_receiver;
using bexec_bench::stdexec_receiver;

bexec::task<int> plain_task_bexec() { co_return 42; }

stdexec::task<int> plain_task_stdexec() { co_return 42; }

bexec::task<int> add_task_bexec() {
  int value = co_await bexec::just(40);
  co_return value + 2;
}

stdexec::task<int> add_task_stdexec() {
  int value = co_await stdexec::just(40);
  co_return value + 2;
}

bexec::task<int> nested_task_bexec() {
  int value = co_await add_task_bexec();
  co_return value + 1;
}

stdexec::task<int> nested_task_stdexec() {
  int value = co_await add_task_stdexec();
  co_return value + 1;
}

bexec::task<void> void_task_bexec(bool& ran) {
  co_await bexec::just();
  ran = true;
  co_return;
}

stdexec::task<void> void_task_stdexec(bool& ran) {
  co_await stdexec::just();
  ran = true;
  co_return;
}

void task_plain_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = bexec::connect(plain_task_bexec(), bench_receiver{&sink});
    bexec::start(operation);
  }
}

// stdexec::task exposes no public connect() to an arbitrary receiver; its
// official consumer path is sync_wait, so the stdexec flavors below drive the
// task through sync_wait instead. task.as_sender compares sync_wait against
// sync_wait on both sides.
void task_plain_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    if (auto result = stdexec::sync_wait(plain_task_stdexec())) {
      auto& [value] = *result;
      sink += static_cast<std::uint64_t>(value);
    }
  }
}

void task_coawait_just_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = bexec::connect(add_task_bexec(), bench_receiver{&sink});
    bexec::start(operation);
  }
}

void task_coawait_just_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    if (auto result = stdexec::sync_wait(add_task_stdexec())) {
      auto& [value] = *result;
      sink += static_cast<std::uint64_t>(value);
    }
  }
}

void task_nested_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = bexec::connect(nested_task_bexec(), bench_receiver{&sink});
    bexec::start(operation);
  }
}

void task_nested_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    if (auto result = stdexec::sync_wait(nested_task_stdexec())) {
      auto& [value] = *result;
      sink += static_cast<std::uint64_t>(value);
    }
  }
}

void task_void_bexec(std::size_t iters, std::uint64_t& sink) {
  std::uint64_t ran = 0;
  for (std::size_t i = 0; i < iters; ++i) {
    bool flag = false;
    auto operation =
        bexec::connect(void_task_bexec(flag), bench_receiver{&sink});
    bexec::start(operation);
    ran += flag;
  }
  sink += ran;
}

void task_void_stdexec(std::size_t iters, std::uint64_t& sink) {
  std::uint64_t ran = 0;
  for (std::size_t i = 0; i < iters; ++i) {
    bool flag = false;
    if (auto result = stdexec::sync_wait(void_task_stdexec(flag))) {
      ran += flag;
      (void)result;
    }
  }
  sink += ran;
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("task.plain.bexec", task_plain_bexec);
  registry.add("task.plain.stdexec", task_plain_stdexec);
  registry.add("task.coawait_just.bexec", task_coawait_just_bexec);
  registry.add("task.coawait_just.stdexec", task_coawait_just_stdexec);
  registry.add("task.nested_1.bexec", task_nested_bexec);
  registry.add("task.nested_1.stdexec", task_nested_stdexec);
  registry.add("task.void.bexec", task_void_bexec);
  registry.add("task.void.stdexec", task_void_stdexec);
  return bexec_bench::run(registry, argc, argv, "task");
}
