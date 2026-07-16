/**
 * @file tests/test_support.hpp
 * @brief Shared GoogleTest helpers and receivers.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides common receiver state, reusable receiver types, and stress controls
 * for the GoogleTest-based test suite.
 */

#pragma once

#ifndef BEXEC_TESTS_TEST_SUPPORT_HPP_
#define BEXEC_TESTS_TEST_SUPPORT_HPP_

#include <gtest/gtest.h>

#include <concepts>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace bexec_tests {

int stress_iterations(int base_iterations);

enum class signal_kind { none, value, error, stopped };

struct shared_state {
  signal_kind signal{signal_kind::none};
  int int_value{0};
  std::string string_value;
  std::exception_ptr exception;
};

struct any_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();

  void set_value() noexcept { state->signal = signal_kind::value; }

  void set_value(int value) noexcept {
    state->signal = signal_kind::value;
    state->int_value = value;
  }

  void set_value(std::unique_ptr<int> value) noexcept {
    state->signal = signal_kind::value;
    state->int_value = *value;
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    state->signal = signal_kind::error;
    if constexpr (std::same_as<std::decay_t<Error>, std::exception_ptr>) {
      state->exception = std::forward<Error>(error);
    }
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }
};

template <class Env>
struct env_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();
  Env env;

  void set_value() noexcept { state->signal = signal_kind::value; }

  template <class Error>
  void set_error(Error&&) noexcept {
    state->signal = signal_kind::error;
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }

  Env get_env() const noexcept { return env; }
};

template <class Variant>
struct variant_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();
  std::shared_ptr<std::optional<Variant>> error =
      std::make_shared<std::optional<Variant>>();

  void set_value() noexcept { state->signal = signal_kind::value; }

  void set_error(Variant value) noexcept {
    state->signal = signal_kind::error;
    *error = std::move(value);
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }
};

}  // namespace bexec_tests
#endif  // BEXEC_TESTS_TEST_SUPPORT_HPP_
