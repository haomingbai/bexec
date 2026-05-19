/**
 * @file examples/split_with_let.cpp
 * @brief Demonstrates replacing old split-style fan-out with let_*.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "example_receiver.hpp"

namespace {

struct value_type {
  value_type(int number_arg, std::string label_arg)
      : number(number_arg), label(std::move(label_arg)) {}

  value_type(const value_type&) = delete;
  value_type& operator=(const value_type&) = delete;
  value_type(value_type&&) noexcept = default;
  value_type& operator=(value_type&&) noexcept = default;

  int number{};
  std::string label;
};

}  // namespace

int main() {
  int source_runs = 0;

  auto value_fanout =
      bexec::just() | bexec::then([&] {
        ++source_runs;
        std::cout << "split source ran " << source_runs << " time(s)\n";
        return 21;
      }) |
      bexec::let_value([](int value) {
        return bexec::when_all(
            bexec::just(value) | bexec::then([](int item) { return item + 1; }),
            bexec::just(value) |
                bexec::then([](int item) { return item * 2; }));
      }) |
      bexec::then([](int plus_one, int doubled) {
        return std::string{"plus_one="} + std::to_string(plus_one) +
               ", doubled=" + std::to_string(doubled);
      });

  bexec_examples::run_sender("split via let_value", std::move(value_fanout));

  bexec::io_context context;
  int async_source_runs = 0;

  auto split_like_async =
      bexec::schedule(context.get_scheduler()) | bexec::then([&] {
        ++async_source_runs;
        std::cout << "split async source ran " << async_source_runs
                  << " time(s)\n";
        return value_type{21, "expensive result"};
      }) |
      bexec::let_value([scheduler = context.get_scheduler()](value_type value) {
        auto shared = std::make_shared<value_type>(std::move(value));
        auto read_plus_one =
            bexec::schedule(scheduler) |
            bexec::then([shared]() -> const value_type& { return *shared; }) |
            bexec::then(
                [](const value_type& value) { return value.number + 1; });
        auto read_doubled =
            bexec::schedule(scheduler) |
            bexec::then([shared]() -> const value_type& { return *shared; }) |
            bexec::then(
                [](const value_type& value) { return value.number * 2; });

        return bexec::when_all(std::move(read_plus_one),
                               std::move(read_doubled)) |
               bexec::then([shared](int plus_one, int doubled) {
                 return std::string{"plus_one="} + std::to_string(plus_one) +
                        ", doubled=" + std::to_string(doubled) +
                        ", label=" + shared->label;
               });
      });

  auto async_operation =
      bexec::connect(std::move(split_like_async),
                     bexec_examples::logging_receiver{"split-like async"});

  bexec::start(async_operation);
  context.run();

  auto error_fanout =
      bexec::just_error(std::string{"timeout"}) |
      bexec::let_error([](std::string reason) {
        return bexec::when_all(bexec::just(std::string{"logged "} + reason),
                               bexec::just(std::string{"fallback ready"}));
      }) |
      bexec::then([](std::string logged, std::string fallback) {
        return logged + "; " + fallback;
      });

  bexec_examples::run_sender("split via let_error", std::move(error_fanout));

  auto stopped_fanout =
      bexec::just_stopped() | bexec::let_stopped([] {
        return bexec::when_all(bexec::just(std::string{"notified stop"}),
                               bexec::just(std::string{"released resources"}));
      }) |
      bexec::then([](std::string notice, std::string cleanup) {
        return notice + "; " + cleanup;
      });

  bexec_examples::run_sender("split via let_stopped",
                             std::move(stopped_fanout));
}
