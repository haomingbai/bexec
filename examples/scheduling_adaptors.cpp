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
  {
    bexec::run_loop target;
    auto starts_operation =
        bexec::connect(bexec::starts_on(target.get_scheduler(), bexec::just(3)),
                       bexec_examples::logging_receiver{"starts_on"});

    bexec::start(starts_operation);
    target.finish();
    target.run();
    std::cout << "target drained starts_on work\n";
  }

  bexec::run_loop target;
  bexec::run_loop caller;

  auto sender = bexec::on(target.get_scheduler(),
                          bexec::just(4) | bexec::then([](int value) {
                            std::cout << "on child ran on target\n";
                            return value + 5;
                          }));

  auto operation = bexec::connect(
      std::move(sender),
      scheduled_logging_receiver{"on final", caller.get_scheduler()});

  bexec::start(operation);
  target.finish();
  target.run();
  std::cout << "target drained on child work\n";
  caller.finish();
  caller.run();
  std::cout << "caller drained final work\n";
}
