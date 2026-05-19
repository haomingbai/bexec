/**
 * @file include/bexec/detail/operation_storage.hpp
 * @brief In-place storage for one of several operation-state types.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_OPERATION_STORAGE_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_OPERATION_STORAGE_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/operation_state.hpp>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace bexec::detail {

template <std::size_t First, std::size_t... Rest>
struct static_max
    : std::integral_constant<std::size_t, ((First > static_max<Rest...>::value)
                                               ? First
                                               : static_max<Rest...>::value)> {
};

template <std::size_t Value>
struct static_max<Value> : std::integral_constant<std::size_t, Value> {};

template <class Operations>
class operation_storage;

template <class... Operations>
class operation_storage<type_list<Operations...>> {
 public:
  operation_storage() noexcept = default;

  operation_storage(const operation_storage&) = delete;
  operation_storage& operator=(const operation_storage&) = delete;
  operation_storage(operation_storage&&) = delete;
  operation_storage& operator=(operation_storage&&) = delete;

  ~operation_storage() { reset(); }

  template <class Operation, class Factory>
  Operation& emplace_from(Factory&& factory) {
    static_assert(type_list_contains_v<Operation, type_list<Operations...>>,
                  "operation type is not listed in this operation_storage");

    reset();
    Operation* operation = ::new (static_cast<void*>(storage_))
        Operation(std::forward<Factory>(factory)());
    destroy_ = &destroy_model<Operation>;
    start_ = &start_model<Operation>;
    return *operation;
  }

  void reset() noexcept {
    if (destroy_ == nullptr) {
      return;
    }

    destroy_(static_cast<void*>(storage_));
    destroy_ = nullptr;
    start_ = nullptr;
  }

  void start() noexcept { start_(static_cast<void*>(storage_)); }

 private:
  template <class Operation>
  static void destroy_model(void* storage) noexcept {
    std::launder(reinterpret_cast<Operation*>(storage))->~Operation();
  }

  template <class Operation>
  static void start_model(void* storage) noexcept {
    bexec::start(*std::launder(reinterpret_cast<Operation*>(storage)));
  }

  static constexpr std::size_t storage_size =
      static_max<1U, sizeof(Operations)...>::value;
  static constexpr std::size_t storage_align =
      static_max<1U, alignof(Operations)...>::value;

  alignas(storage_align) unsigned char storage_[storage_size];
  void (*destroy_)(void*) noexcept {nullptr};
  void (*start_)(void*) noexcept {nullptr};
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_OPERATION_STORAGE_HPP_
