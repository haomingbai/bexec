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

#include <bexec/detail/manual_variant.hpp>
#include <bexec/operation_state.hpp>
#include <utility>

namespace bexec::detail {

template <class Operations>
class operation_storage;

template <class... Operations>
class operation_storage<type_list<Operations...>> {
 public:
  using storage_type = manual_variant<type_list<Operations...>>;

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
    Operation& operation = storage_.template emplace_from<Operation>(
        std::forward<Factory>(factory));
    start_ = &start_model<Operation>;
    return operation;
  }

  void reset() noexcept {
    storage_.reset();
    start_ = nullptr;
  }

  void start() noexcept { start_(storage_); }

 private:
  template <class Operation>
  static void start_model(storage_type& storage) noexcept {
    bexec::start(storage.template get<Operation>());
  }

  storage_type storage_;
  void (*start_)(storage_type&) noexcept {nullptr};
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_OPERATION_STORAGE_HPP_
