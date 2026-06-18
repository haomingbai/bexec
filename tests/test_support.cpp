/**
 * @file tests/test_support.cpp
 * @brief Shared test registration and stress configuration.
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
namespace {

std::vector<registered_test>& mutable_registered_tests() {
  static std::vector<registered_test> tests;
  return tests;
}

}  // namespace

int failures = 0;
std::string_view current_test_case = "<initialization>";

test_registration::test_registration(std::string_view name,
                                     test_category category,
                                     void (*function)()) {
  mutable_registered_tests().push_back({name, category, function});
}

const std::vector<registered_test>& registered_tests() {
  return mutable_registered_tests();
}

std::string_view category_name(test_category category) noexcept {
  switch (category) {
    case test_category::basic:
      return "basic";
    case test_category::integration:
      return "integration";
    case test_category::stress:
      return "stress";
  }
  return "unknown";
}

std::optional<test_category> parse_category(std::string_view value) noexcept {
  if (value == "basic") {
    return test_category::basic;
  }
  if (value == "integration") {
    return test_category::integration;
  }
  if (value == "stress") {
    return test_category::stress;
  }
  return std::nullopt;
}

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
    std::cerr << "invalid BEXEC_STRESS_MULTIPLIER: " << raw_multiplier
              << " (expected integer 1..100)\n";
    ++failures;
    return base_iterations;
  }
  return base_iterations * static_cast<int>(multiplier);
}

}  // namespace bexec_tests
