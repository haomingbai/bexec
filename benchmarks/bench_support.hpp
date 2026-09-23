/**
 * @file benchmarks/bench_support.hpp
 * @brief Minimal self-contained benchmark harness for bexec and stdexec.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides the anti-optimization barriers, the registry, the calibration and
 * timing driver, and a generic result-accumulating receiver shared by the
 * per-feature benchmark executables. Each executable registers benchmark
 * bodies of the form `void(std::size_t iters, std::uint64_t& sink)` and calls
 * bexec_bench::run() from main().
 *
 * Every benchmark case comes in two flavors running the same logic: the
 * `.bexec` flavor drives bexec, and the `.stdexec` flavor drives the stdexec
 * reference implementation, so per-feature overheads can be compared side by
 * side. Where stdexec lacks a directly equivalent piece (the run_loop
 * scheduler), the closest stdexec facility is used and the difference is
 * noted next to the case.
 *
 * Measurement philosophy: neither flavor inserts compiler barriers inside the
 * hot loop. Both libraries are measured under the same conditions and are
 * free to fold sender pipelines into straight-line code — that is the
 * zero-overhead abstraction both aspire to. Only the accumulated sink is
 * anchored after the loop with do_not_optimize so the work cannot be deleted
 * entirely.
 *
 * Modes:
 * - Full mode (default): each case is calibrated so one measured call takes
 *   at least --min-time-ms, then measured --rounds times; the best (lowest)
 *   ns/iter is reported.
 * - Smoke mode (--smoke): the calibration target drops to a couple of
 *   milliseconds with a bounded iteration count so a CI pass only proves the
 *   benchmarks still run, rather than producing trustworthy timings.
 */

#pragma once

#ifndef BEXEC_TESTS_BENCHMARK_BENCH_SUPPORT_HPP_
#define BEXEC_TESTS_BENCHMARK_BENCH_SUPPORT_HPP_

#include <algorithm>
#include <bexec/completion_signatures.hpp>
#include <bexec/receiver.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexec/execution.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace bexec_bench {

// ---------------------------------------------------------------------------
// Anti-optimization barriers
// ---------------------------------------------------------------------------

#if defined(__clang__) || (defined(__GNUC__) && !defined(__ICC))
template <class T>
inline void do_not_optimize(T const& value) noexcept {
  asm volatile("" : : "r,m"(value) : "memory");
}

inline void clobber_memory() noexcept { asm volatile("" : : : "memory"); }
#else
template <class T>
inline void do_not_optimize(T const& value) noexcept {
  const volatile T* sink = &value;
  static_cast<void>(sink);
}

inline void clobber_memory() noexcept {}
#endif

// ---------------------------------------------------------------------------
// Registry and driver
// ---------------------------------------------------------------------------

/** @brief A benchmark body loops over `iters` iterations and accumulates a
 * completion counter into `sink` so the work cannot be optimized away. */
using bench_fn = void (*)(std::size_t iters, std::uint64_t& sink);

struct case_entry {
  std::string name;
  bench_fn fn;
};

class registry {
 public:
  void add(std::string_view name, bench_fn fn) {
    cases_.push_back(case_entry{std::string(name), fn});
  }

  const std::vector<case_entry>& cases() const noexcept { return cases_; }

 private:
  std::vector<case_entry> cases_;
};

inline std::uint64_t now_ns() noexcept {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

struct options {
  bool smoke{false};
  std::string filter;
  std::uint64_t full_time_ns{30'000'000};
  std::uint64_t smoke_time_ns{2'000'000};
  std::size_t smoke_max_iters{5'000};
  std::size_t full_rounds{5};
  std::size_t max_iters{100'000'000};
};

/** @brief Doubles/grows the iteration count until one call takes at least
 * target_ns. Also serves as a warm-up so allocator and cache effects settle. */
inline std::size_t calibrate(bench_fn fn, std::uint64_t target_ns,
                             std::size_t max_iters, std::uint64_t& sink) {
  std::size_t iters = 1;
  for (;;) {
    std::uint64_t const start = now_ns();
    fn(iters, sink);
    std::uint64_t const elapsed = now_ns() - start;
    if (elapsed >= target_ns || iters >= max_iters) {
      return iters;
    }
    double const per_iter =
        elapsed > 0 ? static_cast<double>(elapsed) / static_cast<double>(iters)
                    : 0.0;
    std::size_t next = static_cast<std::size_t>(static_cast<double>(target_ns) /
                                                std::max(per_iter, 1.0) * 1.4);
    next = std::max(next, iters * 2);
    iters = std::min(next, max_iters);
  }
}

inline void print_usage() noexcept {
  std::printf(
      "usage: benchmark [--smoke] [--filter=substr] [--list] "
      "[--min-time-ms=N] [--rounds=N]\n");
}

inline int run(const registry& reg, int argc, char** argv, const char* suite) {
  options opts;
  for (int i = 1; i < argc; ++i) {
    std::string_view const arg{argv[i]};
    if (arg == "--smoke") {
      opts.smoke = true;
    } else if (arg == "--list") {
      for (const case_entry& entry : reg.cases()) {
        std::printf("%s\n", entry.name.c_str());
      }
      return 0;
    } else if (arg.rfind("--filter=", 0) == 0) {
      opts.filter = std::string(arg.substr(std::strlen("--filter=")));
    } else if (arg.rfind("--min-time-ms=", 0) == 0) {
      opts.full_time_ns =
          static_cast<std::uint64_t>(std::strtoull(argv[i] + 14, nullptr, 10)) *
          1'000'000ULL;
    } else if (arg.rfind("--rounds=", 0) == 0) {
      opts.full_rounds =
          static_cast<std::size_t>(std::strtoull(argv[i] + 9, nullptr, 10));
    } else if (arg == "--help") {
      print_usage();
      return 0;
    } else {
      std::fprintf(stderr, "unknown option: %s\n", argv[i]);
      print_usage();
      return 2;
    }
  }

  std::uint64_t const target_ns =
      opts.smoke ? opts.smoke_time_ns : opts.full_time_ns;
  std::size_t const max_iters =
      opts.smoke ? opts.smoke_max_iters : opts.max_iters;
  std::size_t const rounds = opts.smoke ? 1 : opts.full_rounds;

  std::printf("bexec benchmark suite: %s%s\n", suite,
              opts.smoke ? " (smoke)" : "");
  std::uint64_t total_sink = 0;
  for (const case_entry& entry : reg.cases()) {
    if (!opts.filter.empty() &&
        entry.name.find(opts.filter) == std::string::npos) {
      continue;
    }
    std::uint64_t sink = 0;
    std::size_t const iters = calibrate(entry.fn, target_ns, max_iters, sink);
    double best = 0.0;
    for (std::size_t round = 0; round < rounds; ++round) {
      std::uint64_t const start = now_ns();
      entry.fn(iters, sink);
      std::uint64_t const elapsed = now_ns() - start;
      double const per_iter =
          static_cast<double>(elapsed) / static_cast<double>(iters);
      best = best == 0.0 ? per_iter : std::min(best, per_iter);
    }
    do_not_optimize(sink);
    total_sink += sink;
    std::printf("%-44s %10.1f ns/iter   iters=%-9zu rounds=%zu\n",
                entry.name.c_str(), best, iters, rounds);
    std::fflush(stdout);
  }
  do_not_optimize(total_sink);
  return 0;
}

// ---------------------------------------------------------------------------
// Shared benchmark receivers
// ---------------------------------------------------------------------------

/** @brief Receiver accumulating every completion into `sink`. Value payloads
 * must be integrally convertible; special-shaped completions get dedicated
 * receivers inside the corresponding benchmark file.
 *
 * Used with bexec, which falls back to an empty environment when the receiver
 * has no get_env member. */
struct bench_receiver {
  std::uint64_t* sink;

  void set_value() noexcept { ++*sink; }

  template <class Arg, class... Rest>
  void set_value(Arg arg, Rest... rest) noexcept {
    *sink += (static_cast<std::uint64_t>(arg) + ... +
              static_cast<std::uint64_t>(rest));
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    ++*sink;
  }

  void set_stopped() noexcept { ++*sink; }
};

/** @brief The stdexec twin of bench_receiver. stdexec requires a noexcept
 * get_env() member returning a queryable environment, so this twin supplies
 * an empty stdexec environment, plus the C++26 receiver_concept marker. */
struct stdexec_receiver {
  using receiver_concept = stdexec::receiver_tag;

  std::uint64_t* sink;

  void set_value() noexcept { ++*sink; }

  template <class Arg, class... Rest>
  void set_value(Arg arg, Rest... rest) noexcept {
    *sink += (static_cast<std::uint64_t>(arg) + ... +
              static_cast<std::uint64_t>(rest));
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    ++*sink;
  }

  void set_stopped() noexcept { ++*sink; }

  [[nodiscard]] stdexec::env<> get_env() const noexcept { return {}; }
};

}  // namespace bexec_bench

#endif  // BEXEC_TESTS_BENCHMARK_BENCH_SUPPORT_HPP_
