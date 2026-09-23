/**
 * @file benchmarks/just.cpp
 * @brief Benchmarks for the just sender factories and bare connect/start.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexec/execution.hpp>

#include "bench_support.hpp"

namespace {

using bexec_bench::bench_receiver;
using bexec_bench::stdexec_receiver;

void just_value_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = bexec::connect(bexec::just(42), receiver);
    bexec::start(operation);
  }
}

void just_value_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = stdexec::connect(stdexec::just(42), receiver);
    stdexec::start(operation);
  }
}

void just_value_lvalue_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  auto sender = bexec::just(42);
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = bexec::connect(sender, receiver);
    bexec::start(operation);
  }
}

void just_value_lvalue_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  auto sender = stdexec::just(42);
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = stdexec::connect(sender, receiver);
    stdexec::start(operation);
  }
}

void just_multi_value_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = bexec::connect(bexec::just(1, 2, 3), receiver);
    bexec::start(operation);
  }
}

void just_multi_value_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = stdexec::connect(stdexec::just(1, 2, 3), receiver);
    stdexec::start(operation);
  }
}

void just_error_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = bexec::connect(bexec::just_error(7), receiver);
    bexec::start(operation);
  }
}

void just_error_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = stdexec::connect(stdexec::just_error(7), receiver);
    stdexec::start(operation);
  }
}

void just_stopped_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = bexec::connect(bexec::just_stopped(), receiver);
    bexec::start(operation);
  }
}

void just_stopped_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = stdexec::connect(stdexec::just_stopped(), receiver);
    stdexec::start(operation);
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("just.value.bexec", just_value_bexec);
  registry.add("just.value.stdexec", just_value_stdexec);
  registry.add("just.value_lvalue.bexec", just_value_lvalue_bexec);
  registry.add("just.value_lvalue.stdexec", just_value_lvalue_stdexec);
  registry.add("just.multi_value.bexec", just_multi_value_bexec);
  registry.add("just.multi_value.stdexec", just_multi_value_stdexec);
  registry.add("just.error.bexec", just_error_bexec);
  registry.add("just.error.stdexec", just_error_stdexec);
  registry.add("just.stopped.bexec", just_stopped_bexec);
  registry.add("just.stopped.stdexec", just_stopped_stdexec);
  return bexec_bench::run(registry, argc, argv, "just");
}
