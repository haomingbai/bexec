/**
 * @file examples/scheduling_adaptors.cpp
 * @brief Demonstrates starts_on and on scheduling adaptors.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <iostream>
#include <string>
#include <utility>

#include "example_receiver.hpp"

namespace {

struct scheduler_env {
  bexec::run_loop::scheduler scheduler;

  [[nodiscard]] bexec::run_loop::scheduler query(
      bexec::get_scheduler_t) const noexcept {
    return scheduler;
  }

  [[nodiscard]] bexec::run_loop::scheduler query(
      bexec::get_delegation_scheduler_t) const noexcept {
    return scheduler;
  }
};

class scheduled_logging_receiver {
 public:
  scheduled_logging_receiver(std::string label,
                             bexec::run_loop::scheduler scheduler)
      : label_(std::move(label)), env_{scheduler} {}

  void set_value(int value) noexcept {
    std::cout << label_ << ": set_value(" << value << ")\n";
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    std::cout << label_ << ": set_error(...)\n";
  }

  void set_stopped() noexcept { std::cout << label_ << ": set_stopped()\n"; }

  [[nodiscard]] scheduler_env get_env() const noexcept { return env_; }

 private:
  std::string label_;
  scheduler_env env_;
};

}  // namespace

int main() {
  bexec::run_loop target;
  bexec::run_loop caller;

  auto starts_operation =
      bexec::connect(bexec::starts_on(target.get_scheduler(), bexec::just(3)),
                     bexec_examples::logging_receiver{"starts_on"});

  bexec::start(starts_operation);
  auto starts_items = target.run_one();
  std::cout << "target ran " << starts_items << " starts_on item(s)\n";

  auto sender = bexec::on(target.get_scheduler(),
                          bexec::just(4) | bexec::then([](int value) {
                            std::cout << "on child ran on target\n";
                            return value + 5;
                          }));

  auto operation = bexec::connect(
      std::move(sender),
      scheduled_logging_receiver{"on final", caller.get_scheduler()});

  bexec::start(operation);
  auto target_items = target.run_one();
  std::cout << "target ran " << target_items << " on item(s)\n";
  auto caller_items = caller.run_one();
  std::cout << "caller ran " << caller_items << " final item(s)\n";
}
