/**
 * @file include/bexec/generator.hpp
 * @brief Move-only synchronous coroutine generator.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_GENERATOR_HPP_
#define BEXEC_INCLUDE_BEXEC_GENERATOR_HPP_

#include <bexec/detail/config.hpp>
#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace bexec {

/**
 * @brief Single-pass synchronous range produced with co_yield.
 */
template <class T>
  requires std::is_object_v<T>
class generator {
 public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  class iterator {
   public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using reference = const T&;
    using pointer = const T*;

    iterator() noexcept = default;
    explicit iterator(handle_type handle) noexcept : handle_(handle) {}

    reference operator*() const noexcept {
      assert(handle_);
      assert(handle_.promise().value_.has_value());
      return *handle_.promise().value_;
    }

    pointer operator->() const noexcept { return std::addressof(operator*()); }

    iterator& operator++() {
      assert(handle_);
      handle_.resume();
      handle_.promise().rethrow_if_exception();
      return *this;
    }

    void operator++(int) { ++*this; }

    friend bool operator==(const iterator& iterator,
                           std::default_sentinel_t) noexcept {
      return !iterator.handle_ || iterator.handle_.done();
    }

   private:
    handle_type handle_{};
  };

  struct promise_type {
    generator get_return_object() noexcept {
      return generator{handle_type::from_promise(*this)};
    }

    std::suspend_always initial_suspend() const noexcept { return {}; }
    std::suspend_always final_suspend() const noexcept { return {}; }

    template <class Value>
      requires std::constructible_from<T, Value>
    std::suspend_always yield_value(Value&& value) {
      value_.emplace(std::forward<Value>(value));
      return {};
    }

    void return_void() const noexcept {}

    void unhandled_exception() noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      error_ = std::current_exception();
#else
      assert(false);
      BEXEC_DETAIL_UNREACHABLE();
#endif
    }

    template <class Value>
    void await_transform(Value&&) = delete;

    void rethrow_if_exception() {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      if (error_) {
        std::rethrow_exception(error_);
      }
#endif
    }

   private:
    friend class iterator;

    std::optional<T> value_;
    std::exception_ptr error_;
  };

  generator() noexcept = default;
  explicit generator(handle_type handle) noexcept : handle_(handle) {}

  generator(const generator&) = delete;
  generator& operator=(const generator&) = delete;

  generator(generator&& other) noexcept
      : handle_(std::exchange(other.handle_, {})) {}

  generator& operator=(generator&& other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~generator() {
    if (handle_) {
      handle_.destroy();
    }
  }

  iterator begin() {
    if (!handle_) {
      return {};
    }

    assert(!started_);
    started_ = true;
    handle_.resume();
    handle_.promise().rethrow_if_exception();
    return iterator{handle_};
  }

  [[nodiscard]] std::default_sentinel_t end() const noexcept { return {}; }

 private:
  handle_type handle_{};
  bool started_{false};
};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_GENERATOR_HPP_
