/**
 * @file benchmarks/counting_scope.cpp
 * @brief Benchmarks for counting scopes, spawn, and spawn_future.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/counting_scope.hpp>
#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sender.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexec/execution.hpp>
#include <utility>

#include "bench_support.hpp"

namespace {

using bexec_bench::bench_receiver;
using bexec_bench::stdexec_receiver;

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

// bexec join receivers must expose a scheduler through their environment.
struct env_receiver {
  std::uint64_t* sink;
  scheduler_env env;

  void set_value() noexcept { ++*sink; }
  void set_value(int value) noexcept {
    *sink += static_cast<std::uint64_t>(value);
  }
  template <class Error>
  void set_error(Error&&) noexcept {
    ++*sink;
  }
  void set_stopped() noexcept { ++*sink; }

  [[nodiscard]] scheduler_env get_env() const noexcept { return env; }
};

// stdexec's spawn/join/spawn_future drive their completions through the
// get_start_scheduler query in the environment, so the stdexec flavors below
// carry an environment with an inline start scheduler.
struct stdexec_env_receiver {
  using receiver_concept = stdexec::receiver_tag;

  std::uint64_t* sink;
  stdexec::prop<stdexec::get_start_scheduler_t, stdexec::inline_scheduler> env;

  void set_value() noexcept { ++*sink; }
  void set_value(int value) noexcept {
    *sink += static_cast<std::uint64_t>(value);
  }
  template <class Error>
  void set_error(Error&&) noexcept {
    ++*sink;
  }
  void set_stopped() noexcept { ++*sink; }

  [[nodiscard]] const auto& get_env() const noexcept { return env; }
};

// Spawn an immediately-completing child, then join. Both libraries complete
// the join synchronously in this scenario.
void spawn_just_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::counting_scope scope;
    bexec::run_loop loop;
    bexec::spawn(bexec::just(), scope.get_token());
    auto operation = bexec::connect(
        scope.join(), env_receiver{&sink, scheduler_env{loop.get_scheduler()}});
    bexec::start(operation);
    loop.finish();
    loop.run();
  }
}

void spawn_just_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec::inline_scheduler scheduler;
  for (std::size_t i = 0; i < iters; ++i) {
    stdexec::counting_scope scope;
    stdexec::spawn(stdexec::just(), scope.get_token(),
                   stdexec::prop{stdexec::get_start_scheduler, scheduler});
    auto operation = stdexec::connect(
        scope.join(),
        stdexec_env_receiver{
            &sink, stdexec::prop{stdexec::get_start_scheduler, scheduler}});
    stdexec::start(operation);
  }
}

// spawn_future consumed by connecting the returned sender.
void spawn_future_just_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::counting_scope scope;
    bexec::run_loop loop;
    auto future = bexec::spawn_future(bexec::just(42), scope.get_token());
    auto operation = bexec::connect(
        std::move(future),
        env_receiver{&sink, scheduler_env{loop.get_scheduler()}});
    bexec::start(operation);
    loop.finish();
    loop.run();
  }
}

void spawn_future_just_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec::inline_scheduler scheduler;
  for (std::size_t i = 0; i < iters; ++i) {
    stdexec::counting_scope scope;
    auto future = stdexec::spawn_future(
        stdexec::just(42), scope.get_token(),
        stdexec::prop{stdexec::get_start_scheduler, scheduler});
    if (auto result = stdexec::sync_wait(std::move(future))) {
      auto& [value] = *result;
      sink += static_cast<std::uint64_t>(value);
    }
    stdexec::sync_wait(scope.join());
  }
}

// Pure association accounting: associate + disassociate through the token.
void associate_cycle_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::counting_scope scope;
    auto association = scope.get_token().try_associate();
    sink += static_cast<bool>(association);
  }
}

// stdexec requires a scope to be joined before destruction; bexec accepts
// destruction of an unused scope in the open state, so only the stdexec
// flavor joins here.
void associate_cycle_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec::inline_scheduler scheduler;
  for (std::size_t i = 0; i < iters; ++i) {
    stdexec::counting_scope scope;
    auto association = scope.get_token().try_associate();
    sink += static_cast<bool>(association);
    association = {};
    stdexec::sync_wait(scope.join());
  }
}

// Joining an empty scope completes synchronously.
void scope_join_empty_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::counting_scope scope;
    bexec::run_loop loop;
    auto operation = bexec::connect(
        scope.join(), env_receiver{&sink, scheduler_env{loop.get_scheduler()}});
    bexec::start(operation);
    loop.finish();
    loop.run();
  }
}

void scope_join_empty_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec::inline_scheduler scheduler;
  for (std::size_t i = 0; i < iters; ++i) {
    stdexec::counting_scope scope;
    auto operation = stdexec::connect(
        scope.join(),
        stdexec_env_receiver{
            &sink, stdexec::prop{stdexec::get_start_scheduler, scheduler}});
    stdexec::start(operation);
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("counting_scope.associate_cycle.bexec", associate_cycle_bexec);
  registry.add("counting_scope.associate_cycle.stdexec",
               associate_cycle_stdexec);
  registry.add("counting_scope.join_empty.bexec", scope_join_empty_bexec);
  registry.add("counting_scope.join_empty.stdexec", scope_join_empty_stdexec);
  registry.add("counting_scope.spawn_just.bexec", spawn_just_bexec);
  registry.add("counting_scope.spawn_just.stdexec", spawn_just_stdexec);
  registry.add("counting_scope.spawn_future_just.bexec",
               spawn_future_just_bexec);
  registry.add("counting_scope.spawn_future_just.stdexec",
               spawn_future_just_stdexec);
  return bexec_bench::run(registry, argc, argv, "counting_scope");
}
