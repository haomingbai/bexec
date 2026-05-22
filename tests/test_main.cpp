/**
 * @file tests/test_main.cpp
 * @brief Test executable entry point.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Runs every bexec test module, reports accumulated assertion failures, and
 * returns a CTest-compatible status code.
 */

#include <iostream>

#include "test_support.hpp"

namespace bexec_tests {
int failures = 0;
}

int main() {
  bexec_tests::test_concepts();
  bexec_tests::test_completion_signatures();
  bexec_tests::test_counting_scope();
  bexec_tests::test_just();
  bexec_tests::test_then();
  bexec_tests::test_let();
  bexec_tests::test_stop_token();
  bexec_tests::test_env();
  bexec_tests::test_scheduler();
  bexec_tests::test_repeat_until();
  bexec_tests::test_when_all();
  bexec_tests::test_task();

  if (bexec_tests::failures != 0) {
    std::cerr << bexec_tests::failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "all tests passed\n";
  return 0;
}
