/**
 * @file benchmarks/repeat_until.cpp
 * @brief Benchmarks for the repeat_until sender algorithm.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/repeat_until.hpp>
#include <bexec/sender.hpp>
#include <cstddef>
#include <cstdint>
#include <exec/repeat_until.hpp>
#include <stdexec/execution.hpp>

#include "bench_support.hpp"

// The stdexec flavor uses experimental::execution::repeat_until, whose child
// sender completes with a boolean; bexec's repeat_until takes a sender
// factory plus a predicate. Both loop the same number of child completions
// per iteration.

namespace {

using bexec_bench::bench_receiver;
using bexec_bench::stdexec_receiver;

namespace xexec = experimental::execution;

template <std::size_t Rounds>
void repeat_until_n_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  std::size_t count = 0;
  for (std::size_t i = 0; i < iters; ++i) {
    count = 0;
    auto sender =
        bexec::repeat_until([]() noexcept { return bexec::just(1); },
                            [&count]() noexcept { return ++count >= Rounds; });
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

template <std::size_t Rounds>
void repeat_until_n_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  std::size_t count = 0;
  auto child = stdexec::just(1) | stdexec::then([&count](int) noexcept {
                 return ++count >= Rounds;
               });
  for (std::size_t i = 0; i < iters; ++i) {
    count = 0;
    auto sender = xexec::repeat_until(child);
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("repeat_until.rounds_10.bexec", repeat_until_n_bexec<10>);
  registry.add("repeat_until.rounds_10.stdexec", repeat_until_n_stdexec<10>);
  registry.add("repeat_until.rounds_100.bexec", repeat_until_n_bexec<100>);
  registry.add("repeat_until.rounds_100.stdexec", repeat_until_n_stdexec<100>);
  return bexec_bench::run(registry, argc, argv, "repeat_until");
}
