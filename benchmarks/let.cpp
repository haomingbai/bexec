/**
 * @file benchmarks/let.cpp
 * @brief Benchmarks for let_value/let_error sender adaptors.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/just.hpp>
#include <bexec/let.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexec/execution.hpp>
#include <utility>

#include "bench_support.hpp"

namespace {

using bexec_bench::bench_receiver;
using bexec_bench::stdexec_receiver;

auto let_step_bexec(auto sender) noexcept {
  return std::move(sender) | bexec::let_value([](int value) noexcept {
           return bexec::just(value + 1);
         });
}

template <std::size_t Depth>
auto let_chain_bexec(auto sender) noexcept {
  if constexpr (Depth == 0) {
    return sender;
  } else {
    return let_chain_bexec<Depth - 1>(let_step_bexec(std::move(sender)));
  }
}

auto let_step_stdexec(auto sender) noexcept {
  return std::move(sender) | stdexec::let_value([](int value) noexcept {
           return stdexec::just(value + 1);
         });
}

template <std::size_t Depth>
auto let_chain_stdexec(auto sender) noexcept {
  if constexpr (Depth == 0) {
    return sender;
  } else {
    return let_chain_stdexec<Depth - 1>(let_step_stdexec(std::move(sender)));
  }
}

template <std::size_t Depth>
void let_value_nested_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = let_chain_bexec<Depth>(bexec::just(1));
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

template <std::size_t Depth>
void let_value_nested_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = let_chain_stdexec<Depth>(stdexec::just(1));
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

void let_value_immediate_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = bexec::just(1) | bexec::let_value([](int value) noexcept {
                    return bexec::just(value + 1);
                  });
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

void let_value_immediate_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::just(1) | stdexec::let_value([](int value) noexcept {
                    return stdexec::just(value + 1);
                  });
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

void let_error_fired_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender =
        bexec::just_error(1) | bexec::let_error([](int value) noexcept {
          return bexec::just_error(value + 1);
        });
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

void let_error_fired_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender =
        stdexec::just_error(1) | stdexec::let_error([](int value) noexcept {
          return stdexec::just_error(value + 1);
        });
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

void let_error_value_path_bexec(std::size_t iters, std::uint64_t& sink) {
  bench_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = bexec::just(1) | bexec::let_error([](int value) noexcept {
                    return bexec::just_error(value + 1);
                  });
    auto operation = bexec::connect(std::move(sender), receiver);
    bexec::start(operation);
  }
}

void let_error_value_path_stdexec(std::size_t iters, std::uint64_t& sink) {
  stdexec_receiver receiver{&sink};
  for (std::size_t i = 0; i < iters; ++i) {
    auto sender = stdexec::just(1) | stdexec::let_error([](int value) noexcept {
                    return stdexec::just_error(value + 1);
                  });
    auto operation = stdexec::connect(std::move(sender), receiver);
    stdexec::start(operation);
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("let.value_immediate.bexec", let_value_immediate_bexec);
  registry.add("let.value_immediate.stdexec", let_value_immediate_stdexec);
  registry.add("let.value_nested_4.bexec", let_value_nested_bexec<4>);
  registry.add("let.value_nested_4.stdexec", let_value_nested_stdexec<4>);
  registry.add("let.value_nested_8.bexec", let_value_nested_bexec<8>);
  registry.add("let.value_nested_8.stdexec", let_value_nested_stdexec<8>);
  registry.add("let.error_fired.bexec", let_error_fired_bexec);
  registry.add("let.error_fired.stdexec", let_error_fired_stdexec);
  registry.add("let.error_value_path.bexec", let_error_value_path_bexec);
  registry.add("let.error_value_path.stdexec", let_error_value_path_stdexec);
  return bexec_bench::run(registry, argc, argv, "let");
}
