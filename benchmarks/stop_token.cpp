/**
 * @file benchmarks/stop_token.cpp
 * @brief Benchmarks for inplace stop source/token/callback.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-09-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/stop_token.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexec/stop_token.hpp>

#include "bench_support.hpp"

namespace {

using bexec_bench::bench_receiver;
using bexec_bench::stdexec_receiver;

void stop_request_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::inplace_stop_source source;
    source.request_stop();
    sink += source.get_token().stop_requested();
  }
}

void stop_request_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    stdexec::inplace_stop_source source;
    source.request_stop();
    sink += source.get_token().stop_requested();
  }
}

void stop_callback_raii_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::inplace_stop_source source;
    bexec::inplace_stop_callback callback(source.get_token(),
                                          [&sink]() noexcept { ++sink; });
    sink += source.get_token().stop_requested();
  }
}

void stop_callback_raii_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    stdexec::inplace_stop_source source;
    auto fn = [&sink]() noexcept { ++sink; };
    stdexec::inplace_stop_callback<decltype(fn)> callback(source.get_token(),
                                                          fn);
    sink += source.get_token().stop_requested();
  }
}

void stop_callback_fired_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::inplace_stop_source source;
    bexec::inplace_stop_callback callback(source.get_token(),
                                          [&sink]() noexcept { ++sink; });
    source.request_stop();
  }
}

void stop_callback_fired_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    stdexec::inplace_stop_source source;
    auto fn = [&sink]() noexcept { ++sink; };
    stdexec::inplace_stop_callback<decltype(fn)> callback(source.get_token(),
                                                          fn);
    source.request_stop();
  }
}

void stop_callback_fired_8_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::inplace_stop_source source;
    bexec::inplace_stop_callback callback_0(source.get_token(),
                                            [&sink]() noexcept { ++sink; });
    bexec::inplace_stop_callback callback_1(source.get_token(),
                                            [&sink]() noexcept { ++sink; });
    bexec::inplace_stop_callback callback_2(source.get_token(),
                                            [&sink]() noexcept { ++sink; });
    bexec::inplace_stop_callback callback_3(source.get_token(),
                                            [&sink]() noexcept { ++sink; });
    bexec::inplace_stop_callback callback_4(source.get_token(),
                                            [&sink]() noexcept { ++sink; });
    bexec::inplace_stop_callback callback_5(source.get_token(),
                                            [&sink]() noexcept { ++sink; });
    bexec::inplace_stop_callback callback_6(source.get_token(),
                                            [&sink]() noexcept { ++sink; });
    bexec::inplace_stop_callback callback_7(source.get_token(),
                                            [&sink]() noexcept { ++sink; });
    source.request_stop();
  }
}

void stop_callback_fired_8_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    stdexec::inplace_stop_source source;
    auto fn_0 = [&sink]() noexcept { ++sink; };
    stdexec::inplace_stop_callback<decltype(fn_0)> callback_0(
        source.get_token(), fn_0);
    auto fn_1 = [&sink]() noexcept { ++sink; };
    stdexec::inplace_stop_callback<decltype(fn_1)> callback_1(
        source.get_token(), fn_1);
    auto fn_2 = [&sink]() noexcept { ++sink; };
    stdexec::inplace_stop_callback<decltype(fn_2)> callback_2(
        source.get_token(), fn_2);
    auto fn_3 = [&sink]() noexcept { ++sink; };
    stdexec::inplace_stop_callback<decltype(fn_3)> callback_3(
        source.get_token(), fn_3);
    auto fn_4 = [&sink]() noexcept { ++sink; };
    stdexec::inplace_stop_callback<decltype(fn_4)> callback_4(
        source.get_token(), fn_4);
    auto fn_5 = [&sink]() noexcept { ++sink; };
    stdexec::inplace_stop_callback<decltype(fn_5)> callback_5(
        source.get_token(), fn_5);
    auto fn_6 = [&sink]() noexcept { ++sink; };
    stdexec::inplace_stop_callback<decltype(fn_6)> callback_6(
        source.get_token(), fn_6);
    auto fn_7 = [&sink]() noexcept { ++sink; };
    stdexec::inplace_stop_callback<decltype(fn_7)> callback_7(
        source.get_token(), fn_7);
    source.request_stop();
  }
}

void stop_poll_100_bexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    bexec::inplace_stop_source source;
    auto token = source.get_token();
    std::uint64_t requested = 0;
    for (std::size_t poll = 0; poll < 100; ++poll) {
      requested += token.stop_requested();
    }
    source.request_stop();
    requested += token.stop_requested();
    sink += requested;
  }
}

void stop_poll_100_stdexec(std::size_t iters, std::uint64_t& sink) {
  for (std::size_t i = 0; i < iters; ++i) {
    stdexec::inplace_stop_source source;
    auto token = source.get_token();
    std::uint64_t requested = 0;
    for (std::size_t poll = 0; poll < 100; ++poll) {
      requested += token.stop_requested();
    }
    source.request_stop();
    requested += token.stop_requested();
    sink += requested;
  }
}

}  // namespace

int main(int argc, char** argv) {
  bexec_bench::registry registry;
  registry.add("stop_token.request.bexec", stop_request_bexec);
  registry.add("stop_token.request.stdexec", stop_request_stdexec);
  registry.add("stop_token.callback_raii.bexec", stop_callback_raii_bexec);
  registry.add("stop_token.callback_raii.stdexec", stop_callback_raii_stdexec);
  registry.add("stop_token.callback_fired.bexec", stop_callback_fired_bexec);
  registry.add("stop_token.callback_fired.stdexec",
               stop_callback_fired_stdexec);
  registry.add("stop_token.callback_fired_8.bexec",
               stop_callback_fired_8_bexec);
  registry.add("stop_token.callback_fired_8.stdexec",
               stop_callback_fired_8_stdexec);
  registry.add("stop_token.poll_100.bexec", stop_poll_100_bexec);
  registry.add("stop_token.poll_100.stdexec", stop_poll_100_stdexec);
  return bexec_bench::run(registry, argc, argv, "stop_token");
}
