/**
 * @file examples/basic.cpp
 * @brief Executable example covering the main bexec features.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Demonstrates just, then, io_context scheduling, repeat_until, when_all, and
 * coroutine scheduler awaitables.
 */

#include <bexec/bexec.hpp>
#include <iostream>
#include <memory>
#include <utility>

namespace {

struct print_receiver {
  void set_value() noexcept { std::cout << "completed\n"; }

  void set_value(int value) noexcept {
    std::cout << "value: " << value << '\n';
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    std::cout << "error\n";
  }

  void set_stopped() noexcept { std::cout << "stopped\n"; }
};

bexec::task<int> coroutine_example(bexec::io_context::scheduler scheduler) {
  co_await scheduler.schedule_awaitable();
  co_return 42;
}

}  // namespace

int main() {
  {
    auto operation = bexec::connect(
        bexec::just(1) | bexec::then([](int value) { return value + 1; }),
        print_receiver{});
    bexec::start(operation);
  }

  bexec::io_context context;
  auto scheduler = context.get_scheduler();

  {
    auto operation =
        bexec::connect(bexec::schedule(scheduler) |
                           bexec::then([] { std::cout << "scheduled work\n"; }),
                       print_receiver{});
    bexec::start(operation);
    context.run();
  }

  {
    int count = 0;
    auto repeated = bexec::repeat_until(
        [&] { return bexec::just() | bexec::then([&] { ++count; }); },
        [&] { return count == 3; });

    auto operation = bexec::connect(std::move(repeated), print_receiver{});
    bexec::start(operation);
    std::cout << "repeat count: " << count << '\n';
  }

  {
    int count = 0;
    auto first = bexec::schedule(scheduler) | bexec::then([&] { ++count; });
    auto second = bexec::schedule(scheduler) | bexec::then([&] { ++count; });

    auto operation = bexec::connect(
        bexec::when_all(std::move(first), std::move(second)), print_receiver{});
    bexec::start(operation);
    context.run();
    std::cout << "when_all count: " << count << '\n';
  }

  {
    auto task = coroutine_example(scheduler);
    task.start();
    context.run();
    std::cout << "coroutine value: " << task.result() << '\n';
  }
}
