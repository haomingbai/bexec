/**
 * @file examples/into_variant.cpp
 * @brief Demonstrates mapping value alternatives into one variant.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/bexec.hpp>

#include "example_receiver.hpp"
#include "example_senders.hpp"

int main() {
  bexec_examples::run_sender(
      "into_variant int",
      bexec::into_variant(bexec_examples::choice_sender{false}));

  bexec_examples::run_sender(
      "into_variant string",
      bexec_examples::choice_sender{true} | bexec::into_variant());
}
