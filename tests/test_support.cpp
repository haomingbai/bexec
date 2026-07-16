/**
 * @file tests/test_support.cpp
 * @brief Shared GoogleTest stress configuration.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "test_support.hpp"

#include <cerrno>
#include <climits>
#include <cstdlib>

namespace bexec_tests {
int stress_iterations(int base_iterations) {
  const char* raw_multiplier = std::getenv("BEXEC_STRESS_MULTIPLIER");
  if (raw_multiplier == nullptr || *raw_multiplier == '\0') {
    return base_iterations;
  }

  errno = 0;
  char* end = nullptr;
  const long multiplier = std::strtol(raw_multiplier, &end, 10);
  if (errno != 0 || end == raw_multiplier || *end != '\0' || multiplier < 1 ||
      multiplier > 100 ||
      base_iterations > INT_MAX / static_cast<int>(multiplier)) {
    ADD_FAILURE() << "invalid BEXEC_STRESS_MULTIPLIER: " << raw_multiplier
                  << " (expected integer 1..100)";
    return base_iterations;
  }
  return base_iterations * static_cast<int>(multiplier);
}

}  // namespace bexec_tests
