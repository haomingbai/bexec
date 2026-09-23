/**
 * @file benchmarks/then.cpp
 * @brief Benchmarks for then/upon_error/upon_stopped pipelines.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexec/execution.hpp>
#include <utility>

#include "bench_support.hpp"

namespace {

using bexec_bench::bench_receiver;
using bexec_bench::stdexec_receiver;

int noexcept_inc(int value) noexcept { return value + 1; }

int throwing_inc(int value) { return value + 1; }

auto then_step_bexec(auto sender) noexcept {
  return std::move(sender) | bexec::then(noexcept_inc);
}

template <std::size_t Depth>
auto then_chain_bexec(auto sender) noexcept {
  if constexpr (Depth == 0) {
    return sender;
  } else {
    return then_chain_bexec<Depth - 1>(then_step_bexec(std::move(sender)));
  }
}

auto then_step_stdexec(auto sender) noexcept {
  return std::move(sender) | stdexec::then(noexcept_inc);
}

template <std::size_t Depth>
auto then_chain_stdexec(auto sender) noexcept {
  if constexpr (Depth == 0) {
    return sender;
  } else {
    return then_chain_stdexec<Depth - 1>(then_step_stdexec(std::move(sender)));
  }
}

template <std::size_t Depth>
void then_chain_bexec_bench(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = then_chain_bexec<Depth>(bexec::just(1));
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

template <std::size_t Depth>
void then_chain_stdexec_bench(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = then_chain_stdexec<Depth>(stdexec::just(1));
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

void then_may_throw_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = bexec::just(1) | bexec::then(throwing_inc);
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

void then_may_throw_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::just(1) | stdexec::then(throwing_inc);
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

void upon_error_value_path_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = bexec::just(1) |
                  bexec::upon_error([](int value) noexcept { return value; });
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

void upon_error_value_path_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::just(1) |
                  stdexec::upon_error([](int value) noexcept { return value; });
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

void upon_error_fired_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = bexec::just_error(1) |
                  bexec::upon_error([](int value) noexcept { return value; });
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

void upon_error_fired_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::just_error(1) |
                  stdexec::upon_error([](int value) noexcept { return value; });
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

void upon_stopped_value_path_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = bexec::just(1) | bexec::upon_stopped([]() noexcept {});
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

void upon_stopped_value_path_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::just(1) | stdexec::upon_stopped([]() noexcept {});
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("then.chain_1.bexec", then_chain_bexec_bench<1>);
  registry.add("then.chain_1.stdexec", then_chain_stdexec_bench<1>);
  registry.add("then.chain_4.bexec", then_chain_bexec_bench<4>);
  registry.add("then.chain_4.stdexec", then_chain_stdexec_bench<4>);
  registry.add("then.chain_8.bexec", then_chain_bexec_bench<8>);
  registry.add("then.chain_8.stdexec", then_chain_stdexec_bench<8>);
  registry.add("then.may_throw.bexec", then_may_throw_bexec);
  registry.add("then.may_throw.stdexec", then_may_throw_stdexec);
  registry.add("then.upon_error_value_path.bexec", upon_error_value_path_bexec);
  registry.add("then.upon_error_value_path.stdexec",
               upon_error_value_path_stdexec);
  registry.add("then.upon_error_fired.bexec", upon_error_fired_bexec);
  registry.add("then.upon_error_fired.stdexec", upon_error_fired_stdexec);
  registry.add("then.upon_stopped_value_path.bexec",
               upon_stopped_value_path_bexec);
  registry.add("then.upon_stopped_value_path.stdexec",
               upon_stopped_value_path_stdexec);
  return bexec_bench::run(registry, argc, argv, "then");
}
