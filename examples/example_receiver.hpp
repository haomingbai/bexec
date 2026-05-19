/**
 * @file examples/example_receiver.hpp
 * @brief Shared receiver helpers for bexec examples.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-15
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides a small logging receiver and a run_sender helper so individual
 * examples can focus on the sender or algorithm being demonstrated.
 */

#pragma once

#ifndef BEXEC_EXAMPLES_EXAMPLE_RECEIVER_HPP_
#define BEXEC_EXAMPLES_EXAMPLE_RECEIVER_HPP_

#include <bexec/bexec.hpp>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace bexec_examples {

inline void print_value(int value) { std::cout << value; }

inline void print_value(const std::string& value) {
  std::cout << '"' << value << '"';
}

inline void print_value(const std::unique_ptr<int>& value) {
  std::cout << "unique_ptr<int>{";
  if (value) {
    std::cout << *value;
  } else {
    std::cout << "null";
  }
  std::cout << '}';
}

template <class... Args>
void print_value(const std::tuple<Args...>& value) {
  std::cout << "tuple{";
  bool first = true;
  std::apply(
      [&](const auto&... args) {
        ((std::cout << (first ? "" : ", "), print_value(args), first = false),
         ...);
      },
      value);
  std::cout << '}';
}

template <class... Alternatives>
void print_value(const std::variant<Alternatives...>& value) {
  std::cout << "variant{";
  std::visit([](const auto& alternative) { print_value(alternative); }, value);
  std::cout << '}';
}

template <class... Args>
void print_values(const Args&... args) {
  bool first = true;
  ((std::cout << (first ? "" : ", "), print_value(args), first = false), ...);
}

class logging_receiver {
 public:
  explicit logging_receiver(std::string label) : label_(std::move(label)) {}

  void set_value() noexcept { std::cout << label_ << ": set_value()\n"; }

  void set_value(int value) noexcept {
    std::cout << label_ << ": set_value(" << value << ")\n";
  }

  void set_value(std::string value) noexcept {
    std::cout << label_ << ": set_value(\"" << value << "\")\n";
  }

  void set_value(std::unique_ptr<int> value) noexcept {
    std::cout << label_ << ": set_value(unique_ptr<int>{" << *value << "})\n";
  }

  template <class... Alternatives>
  void set_value(std::variant<Alternatives...> value) noexcept {
    std::cout << label_ << ": set_value(";
    print_value(value);
    std::cout << ")\n";
  }

  template <class First, class Second, class... Rest>
  void set_value(First&& first, Second&& second, Rest&&... rest) noexcept {
    std::cout << label_ << ": set_value(";
    print_values(first, second, rest...);
    std::cout << ")\n";
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    std::cout << label_ << ": set_error";
    if constexpr (std::same_as<std::decay_t<Error>, std::string>) {
      std::cout << "(\"" << error << "\")";
    } else if constexpr (std::same_as<std::decay_t<Error>, int>) {
      std::cout << '(' << error << ')';
    } else if constexpr (std::same_as<std::decay_t<Error>,
                                      std::exception_ptr>) {
      std::cout << "(std::exception_ptr)";
    } else {
      std::cout << "(...)";
    }
    std::cout << '\n';
  }

  void set_stopped() noexcept { std::cout << label_ << ": set_stopped()\n"; }

 private:
  std::string label_;
};

template <class Sender>
void run_sender(std::string label, Sender&& sender) {
  auto operation = bexec::connect(std::forward<Sender>(sender),
                                  logging_receiver{std::move(label)});
  bexec::start(operation);
}

}  // namespace bexec_examples
#endif  // BEXEC_EXAMPLES_EXAMPLE_RECEIVER_HPP_
