/**
 * @file benchmarks/on.cpp
 * @brief Benchmarks for starts_on and on scheduling adaptors.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/on.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <cstddef>
#include <cstdint>
#include <exec/inline_scheduler.hpp>
#include <stdexec/execution.hpp>
#include <utility>

#include "bench_support.hpp"

// Both libraries start the child on the given scheduler and, for on(),
// deliver the completion on the receiver's scheduler. bexec is driven through
// run_loops; stdexec uses inline_scheduler, which completes synchronously, so
// the stdexec flavor measures the adaptor logic without queueing.

namespace {

using bexec_bench::bench_receiver;
using bexec_bench::stdexec_receiver;

// bexec receiver: a plain receiver works for starts_on.
void starts_on_run_loop_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::run_loop loop;
    auto sender = bexec::starts_on(loop.get_scheduler(), bexec::just(42));
    auto operation = bexec::connect(std::move(sender), bench_receiver{&sink});
    bexec::start(operation);
    loop.finish();
    loop.run();
  }
}

void starts_on_inline_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec::inline_scheduler scheduler;
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::starts_on(scheduler, stdexec::just(42));
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

// bexec receiver environment carrying a scheduler, needed by on() to know
// where the final completion must be delivered.
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

struct env_receiver {
  std::uint64_t* sink;
  scheduler_env env;

  void set_value(int value) noexcept {
    *sink += static_cast<std::uint64_t>(value);
  }
  void set_value() noexcept { ++*sink; }
  template <class Error>
  void set_error(Error&&) noexcept {
    ++*sink;
  }
  void set_stopped() noexcept { ++*sink; }

  [[nodiscard]] scheduler_env get_env() const noexcept { return env; }
};

void on_run_loop_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::run_loop target;
    bexec::run_loop final_loop;
    env_receiver receiver{&sink, scheduler_env{final_loop.get_scheduler()}};
    auto sender = bexec::on(target.get_scheduler(), bexec::just(42));
    auto operation = bexec::connect(std::move(sender), std::move(receiver));
    bexec::start(operation);
    target.finish();
    target.run();
    final_loop.finish();
    final_loop.run();
  }
}

struct stdexec_env_receiver {
  using receiver_concept = stdexec::receiver_tag;

  std::uint64_t* sink;
  stdexec::prop<stdexec::get_start_scheduler_t, stdexec::inline_scheduler> env;

  void set_value(int value) noexcept {
    *sink += static_cast<std::uint64_t>(value);
  }
  void set_value() noexcept { ++*sink; }
  template <class Error>
  void set_error(Error&&) noexcept {
    ++*sink;
  }
  void set_stopped() noexcept { ++*sink; }

  [[nodiscard]] const auto& get_env() const noexcept { return env; }
};

void on_inline_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec::inline_scheduler scheduler;
  stdexec_env_receiver receiver{
      &sink, stdexec::prop{stdexec::get_start_scheduler, scheduler}};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::on(scheduler, stdexec::just(42));
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

void on_run_loop_then_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::run_loop target;
    bexec::run_loop final_loop;
    env_receiver receiver{&sink, scheduler_env{final_loop.get_scheduler()}};
    auto sender = bexec::on(target.get_scheduler(),
                            bexec::just(20) | bexec::then([](int v) noexcept {
                              return v * 2 + 2;
                            }));
    auto operation = bexec::connect(std::move(sender), std::move(receiver));
    bexec::start(operation);
    target.finish();
    target.run();
    final_loop.finish();
    final_loop.run();
  }
}

void on_inline_then_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec::inline_scheduler scheduler;
  stdexec_env_receiver receiver{
      &sink, stdexec::prop{stdexec::get_start_scheduler, scheduler}};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::on(
        scheduler, stdexec::just(20) |
                       stdexec::then([](int v) noexcept { return v * 2 + 2; }));
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("on.starts_on.bexec", starts_on_run_loop_bexec);
  registry.add("on.starts_on.stdexec", starts_on_inline_stdexec);
  registry.add("on.on.bexec", on_run_loop_bexec);
  registry.add("on.on.stdexec", on_inline_stdexec);
  registry.add("on.on_then.bexec", on_run_loop_then_bexec);
  registry.add("on.on_then.stdexec", on_inline_then_stdexec);
  return bexec_bench::run(registry, argc, argv, "on");
}
