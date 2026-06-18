/**
 * @file tests/test_runner.cpp
 * @brief Feature-test executable entry point.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <exception>
#include <iostream>
#include <string_view>

#include "test_support.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <basic|integration|stress>\n";
    return 2;
  }

  const auto category = bexec_tests::parse_category(argv[1]);
  if (!category) {
    std::cerr << "unknown test category: " << argv[1] << '\n';
    return 2;
  }

  int executed = 0;
  for (const auto& test : bexec_tests::registered_tests()) {
    if (test.category != *category) {
      continue;
    }

    ++executed;
    bexec_tests::current_test_case = test.name;
    const int failures_before = bexec_tests::failures;
    try {
      test.function();
    } catch (const std::exception& error) {
      std::cerr << test.name << ": unexpected exception: " << error.what()
                << '\n';
      ++bexec_tests::failures;
    } catch (...) {
      std::cerr << test.name << ": unexpected non-standard exception\n";
      ++bexec_tests::failures;
    }

    if (bexec_tests::failures != failures_before) {
      std::cerr << "[failed] " << test.name << '\n';
    }
  }

  if (executed == 0) {
    std::cerr << "no " << bexec_tests::category_name(*category)
              << " tests registered\n";
    return 2;
  }

  if (bexec_tests::failures != 0) {
    std::cerr << bexec_tests::failures << " test failure(s) across " << executed
              << " case(s)\n";
    return 1;
  }

  std::cout << executed << ' ' << bexec_tests::category_name(*category)
            << " test case(s) passed\n";
  return 0;
}
