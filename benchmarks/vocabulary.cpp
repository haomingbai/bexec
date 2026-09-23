/**
 * @file benchmarks/vocabulary.cpp
 * @brief Benchmarks for the environment/query vocabulary layer.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/env.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/stop_token.hpp>
#include <cstddef>
#include <cstdint>
#include <exec/inline_scheduler.hpp>
#include <stdexec/execution.hpp>

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

struct env_receiver {
  std::uint64_t* sink;
  scheduler_env env;

  void set_value() noexcept { ++*sink; }
  template <class Error>
  void set_error(Error&&) noexcept {
    ++*sink;
  }
  void set_stopped() noexcept { ++*sink; }

  [[nodiscard]] scheduler_env get_env() const noexcept { return env; }
};

// get_env plus a tag query on a user environment.
void env_query_scheduler_bexec(std::size_t iters, std::uint64_t& sink) {
  env_receiver receiver{&sink, scheduler_env{}};
  for (std::size_t i = 0; i < iters; ++i) {
    auto env = bexec::get_env(receiver);
    auto scheduler = bexec::query(env, bexec::get_scheduler);
    sink += scheduler == scheduler_env{}.scheduler;
  }
}

void env_query_scheduler_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec::inline_scheduler scheduler;
  auto env = stdexec::prop{stdexec::get_scheduler, scheduler};
  for (std::size_t i = 0; i < iters; ++i) {
    auto queried = stdexec::get_scheduler(env);
    sink += queried == scheduler;
  }
}

// get_stop_token on environments without one falls back to never_stop_token.
void env_query_stop_token_fallback_bexec(std::size_t iters,
                                         std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto env = bexec::get_env(receiver);
    auto token = bexec::get_stop_token(env);
    sink += token.stop_requested();
  }
}

void env_query_stop_token_fallback_stdexec(std::size_t iters,
                                           std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto env = stdexec::get_env(receiver);
    auto token = stdexec::get_stop_token(env);
    sink += token.stop_requested();
  }
}

// get_stop_token on an environment that carries one.
void env_query_stop_token_present_bexec(std::size_t iters,
                                        std::uint64_t& sink) {
  bexec::inplace_stop_source source;
  bexec::env_with_stop_token env{source.get_token()};
  for (std::size_t i = 0; i < iters; ++i) {
    auto token = bexec::get_stop_token(env);
    sink += token.stop_requested();
  }
}

void env_query_stop_token_present_stdexec(std::size_t iters,
                                          std::uint64_t& sink) {
  stdexec::inplace_stop_source source;
  auto env = stdexec::prop{stdexec::get_stop_token, source.get_token()};
  for (std::size_t i = 0; i < iters; ++i) {
    auto token = stdexec::get_stop_token(env);
    sink += token.stop_requested();
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("vocabulary.query_scheduler.bexec", env_query_scheduler_bexec);
  registry.add("vocabulary.query_scheduler.stdexec",
               env_query_scheduler_stdexec);
  registry.add("vocabulary.stop_token_fallback.bexec",
               env_query_stop_token_fallback_bexec);
  registry.add("vocabulary.stop_token_fallback.stdexec",
               env_query_stop_token_fallback_stdexec);
  registry.add("vocabulary.stop_token_present.bexec",
               env_query_stop_token_present_bexec);
  registry.add("vocabulary.stop_token_present.stdexec",
               env_query_stop_token_present_stdexec);
  return bexec_bench::run(registry, argc, argv, "vocabulary");
}
