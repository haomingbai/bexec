/**
 * @file benchmarks/into_variant.cpp
 * @brief Benchmarks for the into_variant sender adaptor.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/into_variant.hpp>
#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexec/execution.hpp>

#include "bench_support.hpp"

namespace {

// Receives the std::variant-of-tuples (bexec) or the stdexec variant of
// tuples (stdexec) produced by into_variant; the alternative index is
// accumulated so the completion cannot be optimized away.
struct variant_receiver {
  using receiver_concept = stdexec::receiver_tag;

  std::uint64_t* sink;

  template <class Variant>
  void set_value(Variant&& value) noexcept {
    *sink += static_cast<std::uint64_t>(value.index()) + 1;
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    ++*sink;
  }

  void set_stopped() noexcept { ++*sink; }
};

void into_variant_single_bexec(std::size_t iters, std::uint64_t& sink) {
  variant_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = bexec::into_variant(bexec::just(42));
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

void into_variant_single_stdexec(std::size_t iters, std::uint64_t& sink) {
  variant_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::into_variant(stdexec::just(42));
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

void into_variant_multi_bexec(std::size_t iters, std::uint64_t& sink) {
  variant_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = bexec::into_variant(bexec::just(1, 2, 3));
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

void into_variant_multi_stdexec(std::size_t iters, std::uint64_t& sink) {
  variant_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::into_variant(stdexec::just(1, 2, 3));
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

void into_variant_lvalue_bexec(std::size_t iters, std::uint64_t& sink) {
  variant_receiver receiver{&sink};
  auto sender = bexec::into_variant(bexec::just(42));
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = bexec::connect(sender, receiver);
    bexec::start(operation);
  }
}

void into_variant_lvalue_stdexec(std::size_t iters, std::uint64_t& sink) {
  variant_receiver receiver{&sink};
  auto sender = stdexec::into_variant(stdexec::just(42));
  for (std::size_t i = 0; i < iters; ++i) {
    auto operation = stdexec::connect(sender, receiver);
    stdexec::start(operation);
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("into_variant.single.bexec", into_variant_single_bexec);
  registry.add("into_variant.single.stdexec", into_variant_single_stdexec);
  registry.add("into_variant.multi.bexec", into_variant_multi_bexec);
  registry.add("into_variant.multi.stdexec", into_variant_multi_stdexec);
  registry.add("into_variant.lvalue.bexec", into_variant_lvalue_bexec);
  registry.add("into_variant.lvalue.stdexec", into_variant_lvalue_stdexec);
  return bexec_bench::run(registry, argc, argv, "into_variant");
}
