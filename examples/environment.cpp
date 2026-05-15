/**
 * @file examples/environment.cpp
 * @brief Demonstrates receiver environments and stop-token queries.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <iostream>
#include <utility>

namespace {

class stop_receiver {
 public:
  explicit stop_receiver(bexec::inplace_stop_token token) : env_(token) {}

  void set_value() noexcept { std::cout << "environment: set_value()\n"; }

  template <class Error>
  void set_error(Error&&) noexcept {
    std::cout << "environment: set_error(...)\n";
  }

  void set_stopped() noexcept { std::cout << "environment: set_stopped()\n"; }

  bexec::env_with_stop_token<> get_env() const noexcept { return env_; }

 private:
  bexec::env_with_stop_token<> env_;
};

}  // namespace

int main() {
  bexec::inplace_stop_source source;
  stop_receiver receiver{source.get_token()};

  auto env = bexec::get_env(receiver);
  auto token = bexec::get_stop_token(env);
  std::cout << "stop requested before: " << token.stop_requested() << '\n';

  source.request_stop();
  std::cout << "stop requested after: " << token.stop_requested() << '\n';

  auto operation = bexec::connect(bexec::just(), std::move(receiver));
  bexec::start(operation);
}
