/**
 * @file examples/sync_wait.cpp
 * @brief Demonstrates this_thread::sync_wait helpers.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>
#include <iostream>
#include <string>

#include "example_receiver.hpp"
#include "example_senders.hpp"

int main() {
  auto values = bexec::this_thread::sync_wait(
      bexec::when_all(bexec::just(1), bexec::just(std::string{"ok"})));

  if (values) {
    std::cout << "sync_wait value: ";
    bexec_examples::print_value(*values);
    std::cout << '\n';
  }

  auto stopped = bexec::this_thread::sync_wait(bexec::just_stopped());
  std::cout << "sync_wait stopped: " << !stopped.has_value() << '\n';

  try {
    (void)bexec::this_thread::sync_wait(
        bexec::just_error(std::string{"sync error"}));
  } catch (const std::string& error) {
    std::cout << "sync_wait error: \"" << error << "\"\n";
  }

  auto variant = bexec::this_thread::sync_wait_with_variant(
      bexec_examples::choice_sender{true});
  if (variant) {
    std::cout << "sync_wait_with_variant value: ";
    bexec_examples::print_value(*variant);
    std::cout << '\n';
  }
}
