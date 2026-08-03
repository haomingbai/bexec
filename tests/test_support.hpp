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

#include <bexec/completion_signatures.hpp>
#include <bexec/receiver.hpp>
#include <concepts>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
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

  template <class... Args>
  void set_value(Args&&...) noexcept {
    state->signal = signal_kind::value;
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

/**
 * @brief Value whose move/copy construction throws.
 *
 * Default construction is fine; every move or copy throws a runtime_error.
 * Used to exercise the "value construction may throw" branch of set_x
 * implementations (the else branch of
 * `if constexpr (std::is_nothrow_constructible_v<...>)`).
 */
struct throwing_value {
  throwing_value() = default;
  throwing_value(const throwing_value&) {
    throw std::runtime_error("throwing_value copy");
  }
  throwing_value(throwing_value&&) {
    throw std::runtime_error("throwing_value move");
  }
};

/**
 * @brief Sender that completes with a throwing_value in start().
 *
 * The value is default-constructed at start time, so constructing the sender
 * itself never throws; only the receiver-side storage move throws.
 */
struct throwing_value_sender {
  struct op_base {};

  template <class Receiver>
  struct op {
    Receiver receiver;

    void start() noexcept {
      bexec::set_value(std::move(receiver), throwing_value{});
    }
  };

  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(throwing_value)>;

  template <class Receiver>
  op<Receiver> connect(Receiver&& receiver) {
    return op<Receiver>{std::forward<Receiver>(receiver)};
  }
};

/**
 * @brief Sender whose connect() always throws.
 *
 * Exercises the "connect may throw" branch of adaptors that wrap child
 * connect/start (e.g. when_all/let/on operation states).
 */
struct throwing_connect_sender {
  struct op {
    void start() noexcept {}
  };

  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t()>;

  template <class Receiver>
  op connect(Receiver&&) {
    throw std::runtime_error("throwing_connect_sender connect");
  }
};

/**
 * @brief Scheduler whose schedule() sender throws on connect.
 *
 * Exercises the schedule-connect throw branch of starts_on/on.
 */
struct throwing_schedule_scheduler {
  struct op {
    void start() noexcept {}
  };

  struct sender {
    using completion_signatures =
        bexec::completion_signatures<bexec::set_value_t()>;

    template <class Receiver>
    op connect(Receiver&&) {
      throw std::runtime_error("throwing_schedule connect");
    }
  };

  [[nodiscard]] sender schedule() const { return {}; }

  friend bool operator==(throwing_schedule_scheduler,
                         throwing_schedule_scheduler) noexcept = default;
};

}  // namespace bexec_tests
#endif  // BEXEC_TESTS_TEST_SUPPORT_HPP_
