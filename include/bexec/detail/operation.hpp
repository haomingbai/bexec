/**
 * @file include/bexec/detail/operation.hpp
 * @brief Internal operation-state forwarding helper.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides a thin operation wrapper that forwards start() to an owned child
 * operation through bexec::start.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_OPERATION_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_OPERATION_HPP_

#include <bexec/operation_state.hpp>
#include <functional>
#include <utility>

namespace bexec::detail {

template <class Operation>
class pass_through_operation {
 public:
  explicit pass_through_operation(Operation operation)
      : operation_(std::move(operation)) {}

  template <class Factory>
  explicit pass_through_operation(std::in_place_t, Factory&& factory)
      : operation_(std::invoke(std::forward<Factory>(factory))) {}

  void start() noexcept { bexec::start(operation_); }

 private:
  Operation operation_;
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_OPERATION_HPP_
