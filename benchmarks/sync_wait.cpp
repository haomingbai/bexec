/**
 * @file benchmarks/sync_wait.cpp
 * @brief Benchmarks for this_thread::sync_wait round trips.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/sync_wait.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexec/execution.hpp>
#include <utility>

#include "bench_support.hpp"

namespace {

using bexec_bench::bench_receiver;
using bexec_bench::stdexec_receiver;

void sync_wait_just_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    if (auto result = bexec::this_thread::sync_wait(bexec::just(42))) {
      auto& [value] = *result;
      sink += static_cast<std::uint64_t>(value);
    }
  }
}

void sync_wait_just_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    if (auto result = stdexec::sync_wait(stdexec::just(42))) {
      auto& [value] = *result;
      sink += static_cast<std::uint64_t>(value);
    }
  }
}

void sync_wait_just_void_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    sink += bexec::this_thread::sync_wait(bexec::just()).has_value();
  }
}

void sync_wait_just_void_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    sink += stdexec::sync_wait(stdexec::just()).has_value();
  }
}

void sync_wait_then_chain_4_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = bexec::just(1) |
                  bexec::then([](int v) noexcept { return v + 1; }) |
                  bexec::then([](int v) noexcept { return v + 1; }) |
                  bexec::then([](int v) noexcept { return v + 1; }) |
                  bexec::then([](int v) noexcept { return v + 1; });
    if (auto result = bexec::this_thread::sync_wait(std::move(sender))) {
      auto& [value] = *result;
      sink += static_cast<std::uint64_t>(value);
    }
  }
}

void sync_wait_then_chain_4_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::just(1) |
                  stdexec::then([](int v) noexcept { return v + 1; }) |
                  stdexec::then([](int v) noexcept { return v + 1; }) |
                  stdexec::then([](int v) noexcept { return v + 1; }) |
                  stdexec::then([](int v) noexcept { return v + 1; });
    if (auto result = stdexec::sync_wait(std::move(sender))) {
      auto& [value] = *result;
      sink += static_cast<std::uint64_t>(value);
    }
  }
}

void sync_wait_when_all_4_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    if (auto result = bexec::this_thread::sync_wait(bexec::when_all(
            bexec::just(1), bexec::just(2), bexec::just(3), bexec::just(4)))) {
      auto& [a, b, c, d] = *result;
      sink += static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b) +
              static_cast<std::uint64_t>(c) + static_cast<std::uint64_t>(d);
    }
  }
}

void sync_wait_when_all_4_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    if (auto result = stdexec::sync_wait(
            stdexec::when_all(stdexec::just(1), stdexec::just(2),
                              stdexec::just(3), stdexec::just(4)))) {
      auto& [a, b, c, d] = *result;
      sink += static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b) +
              static_cast<std::uint64_t>(c) + static_cast<std::uint64_t>(d);
    }
  }
}

void sync_wait_stopped_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    sink += !bexec::this_thread::sync_wait(bexec::just_stopped()).has_value();
  }
}

// stdexec::sync_wait requires a value completion path, so the stdexec flavor
// wraps the child in stopped_as_optional: a stopped completion surfaces as an
// engaged sync_wait whose inner optional is empty.
void sync_wait_stopped_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    if (auto result = stdexec::sync_wait(
            stdexec::stopped_as_optional(stdexec::just(1)))) {
      auto& [value] = *result;
      sink += !value.has_value();
    }
  }
}

void sync_wait_with_variant_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    if (auto result =
            bexec::this_thread::sync_wait_with_variant(bexec::just(42))) {
      sink += static_cast<std::uint64_t>(result->index()) + 1;
    }
  }
}

void sync_wait_with_variant_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    if (auto result = stdexec::sync_wait_with_variant(stdexec::just(42))) {
      sink += static_cast<std::uint64_t>(result->index()) + 1;
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("sync_wait.just.bexec", sync_wait_just_bexec);
  registry.add("sync_wait.just.stdexec", sync_wait_just_stdexec);
  registry.add("sync_wait.just_void.bexec", sync_wait_just_void_bexec);
  registry.add("sync_wait.just_void.stdexec", sync_wait_just_void_stdexec);
  registry.add("sync_wait.then_chain_4.bexec", sync_wait_then_chain_4_bexec);
  registry.add("sync_wait.then_chain_4.stdexec",
               sync_wait_then_chain_4_stdexec);
  registry.add("sync_wait.when_all_4.bexec", sync_wait_when_all_4_bexec);
  registry.add("sync_wait.when_all_4.stdexec", sync_wait_when_all_4_stdexec);
  registry.add("sync_wait.stopped.bexec", sync_wait_stopped_bexec);
  registry.add("sync_wait.stopped.stdexec", sync_wait_stopped_stdexec);
  registry.add("sync_wait.with_variant.bexec", sync_wait_with_variant_bexec);
  registry.add("sync_wait.with_variant.stdexec",
               sync_wait_with_variant_stdexec);
  return bexec_bench::run(registry, argc, argv, "sync_wait");
}
