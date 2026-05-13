/**
 * @file include/bexec/just.hpp
 * @brief Synchronous sender factories for immediate completions.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines just, just_error, and just_stopped senders that complete
 * synchronously with value, error, or stopped receiver signals.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_JUST_HPP_
#define BEXEC_INCLUDE_BEXEC_JUST_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/receiver.hpp>
#include <tuple>
#include <type_traits>
#include <utility>

namespace bexec {

/**
 * @brief Sender produced by just(values...).
 */
template <class... Values>
class just_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<set_value_t(Values...)>;

  explicit just_sender(Values... values) : values_(std::move(values)...) {}

  template <class Receiver>
  class operation {
   public:
    operation(std::tuple<Values...> values, Receiver receiver)
        : values_(std::move(values)), receiver_(std::move(receiver)) {}

    /** @brief Completes synchronously with set_value(values...). */
    void start() noexcept {
      std::apply(
          [this](auto&... values) {
            bexec::set_value(std::move(receiver_), std::move(values)...);
          },
          values_);
    }

   private:
    std::tuple<Values...> values_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return operation<Receiver>{std::move(values_), std::move(receiver)};
  }

  template <class Receiver>
    requires((std::copy_constructible<Values> && ...))
  auto connect(Receiver receiver) const& {
    return operation<Receiver>{values_, std::move(receiver)};
  }

 private:
  std::tuple<Values...> values_;
};

/**
 * @brief Sender produced by just_error(error).
 */
template <class Error>
class just_error_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<set_error_t(Error)>;

  explicit just_error_sender(Error error) : error_(std::move(error)) {}

  template <class Receiver>
  class operation {
   public:
    operation(Error error, Receiver receiver)
        : error_(std::move(error)), receiver_(std::move(receiver)) {}

    /** @brief Completes synchronously with set_error(error). */
    void start() noexcept {
      bexec::set_error(std::move(receiver_), std::move(error_));
    }

   private:
    Error error_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return operation<Receiver>{std::move(error_), std::move(receiver)};
  }

  template <class Receiver>
    requires std::copy_constructible<Error>
  auto connect(Receiver receiver) const& {
    return operation<Receiver>{error_, std::move(receiver)};
  }

 private:
  Error error_;
};

/**
 * @brief Sender produced by just_stopped().
 */
class just_stopped_sender {
 public:
  using completion_signatures = bexec::completion_signatures<set_stopped_t()>;

  template <class Receiver>
  class operation {
   public:
    explicit operation(Receiver receiver) : receiver_(std::move(receiver)) {}

    /** @brief Completes synchronously with set_stopped(). */
    void start() noexcept { bexec::set_stopped(std::move(receiver_)); }

   private:
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{std::move(receiver)};
  }
};

/**
 * @brief Function object that creates just senders.
 */
struct just_t {
  template <class... Values>
  [[nodiscard]] auto operator()(Values&&... values) const {
    return just_sender<std::decay_t<Values>...>{
        std::forward<Values>(values)...};
  }
};

/**
 * @brief Function object that creates just_error senders.
 */
struct just_error_t {
  template <class Error>
  [[nodiscard]] auto operator()(Error&& error) const {
    return just_error_sender<std::decay_t<Error>>{std::forward<Error>(error)};
  }
};

/**
 * @brief Function object that creates just_stopped senders.
 */
struct just_stopped_t {
  [[nodiscard]] just_stopped_sender operator()() const { return {}; }
};

inline constexpr just_t just{};
inline constexpr just_error_t just_error{};
inline constexpr just_stopped_t just_stopped{};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_JUST_HPP_
