/**
 * @file benchmarks/when_all.cpp
 * @brief Benchmarks for the when_all sender algorithm driven directly.
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
#include <bexec/when_all.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexec/execution.hpp>
#include <utility>

#include "bench_support.hpp"

namespace {

using bexec_bench::bench_receiver;
using bexec_bench::stdexec_receiver;

template <std::size_t Children>
void when_all_just_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto make_children =
        [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
          return bexec::when_all(((void)Indices, bexec::just(1))...);
        };
    auto sender = make_children(std::make_index_sequence<Children>{});
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

template <std::size_t Children>
void when_all_just_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto make_children =
        [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
          return stdexec::when_all(((void)Indices, stdexec::just(1))...);
        };
    auto sender = make_children(std::make_index_sequence<Children>{});
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

template <std::size_t Children>
void when_all_then_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto make_children =
        [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
          return bexec::when_all(
              (((void)Indices, bexec::just(1)) |
               bexec::then([](int v) noexcept { return v + 1; }))...);
        };
    auto sender = make_children(std::make_index_sequence<Children>{});
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

template <std::size_t Children>
void when_all_then_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto make_children =
        [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
          return stdexec::when_all(
              (((void)Indices, stdexec::just(1)) |
               stdexec::then([](int v) noexcept { return v + 1; }))...);
        };
    auto sender = make_children(std::make_index_sequence<Children>{});
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("when_all.just_1.bexec", when_all_just_bexec<1>);
  registry.add("when_all.just_1.stdexec", when_all_just_stdexec<1>);
  registry.add("when_all.just_2.bexec", when_all_just_bexec<2>);
  registry.add("when_all.just_2.stdexec", when_all_just_stdexec<2>);
  registry.add("when_all.just_8.bexec", when_all_just_bexec<8>);
  registry.add("when_all.just_8.stdexec", when_all_just_stdexec<8>);
  registry.add("when_all.then_8.bexec", when_all_then_bexec<8>);
  registry.add("when_all.then_8.stdexec", when_all_then_stdexec<8>);
  return bexec_bench::run(registry, argc, argv, "when_all");
}
