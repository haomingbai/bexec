/**
 * @file benchmarks/run_loop.cpp
 * @brief Benchmarks for the run_loop scheduler round trip.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <array>
#include <bexec/operation_state.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sender.hpp>
#include <cstddef>
#include <cstdint>
#include <exec/inline_scheduler.hpp>
#include <optional>
#include <stdexec/execution.hpp>
#include <utility>

#include "bench_support.hpp"

// bexec has a stack-owned run_loop scheduler; stdexec's closest single-thread
// counterpart is its inline_scheduler, which completes synchronously without
// a queue. Both sides therefore measure one connect/start pair through the
// cheapest scheduler each library offers, with bexec additionally paying for
// the enqueue/drain cycle.

namespace {

using bexec_bench::bench_receiver;
using bexec_bench::stdexec_receiver;

void run_loop_single_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::run_loop loop;
    auto operation = bexec::connect(bexec::schedule(loop.get_scheduler()),
                                    bench_receiver{&sink});
    bexec::start(operation);
    loop.finish();
    loop.run();
  }
}

void run_loop_single_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec::inline_scheduler scheduler;
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation =
        stdexec::connect(stdexec::schedule(scheduler), stdexec_receiver{&sink});
    stdexec::start(operation);
  }
}

// Enqueues a batch of schedule operations before draining them with one
// finish()/run() pair, measuring enqueue throughput together with the batched
// pop/execute path.
template <std::size_t Batch>
void run_loop_batch_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::run_loop loop;
    auto scheduler = loop.get_scheduler();
    using operation_type = decltype(bexec::connect(
        bexec::schedule(scheduler), std::declval<bench_receiver>()));
    std::array<std::optional<operation_type>, Batch> slots;
    for (auto& slot : slots) {
      slot.emplace(loop, bench_receiver{&sink});
      bexec::start(*slot);
    }
    loop.finish();
    loop.run();
  }
}

template <std::size_t Batch>
void run_loop_batch_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec::inline_scheduler scheduler;
  for (std::size_t i = 0; i < iters; ++i) {
    for (std::size_t child = 0; child < Batch; ++child) {
      auto operation = stdexec::connect(stdexec::schedule(scheduler),
                                        stdexec_receiver{&sink});
      stdexec::start(operation);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("run_loop.single.bexec", run_loop_single_bexec);
  registry.add("run_loop.single.stdexec", run_loop_single_stdexec);
  registry.add("run_loop.batch_8.bexec", run_loop_batch_bexec<8>);
  registry.add("run_loop.batch_8.stdexec", run_loop_batch_stdexec<8>);
  registry.add("run_loop.batch_64.bexec", run_loop_batch_bexec<64>);
  registry.add("run_loop.batch_64.stdexec", run_loop_batch_stdexec<64>);
  return bexec_bench::run(registry, argc, argv, "run_loop");
}
