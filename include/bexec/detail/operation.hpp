#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_OPERATION_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_OPERATION_HPP_

#include <bexec/operation_state.hpp>
#include <utility>

namespace bexec::detail {

template <class Operation>
class pass_through_operation {
 public:
  explicit pass_through_operation(Operation operation)
      : operation_(std::move(operation)) {}

  void start() noexcept { bexec::start(operation_); }

 private:
  Operation operation_;
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_OPERATION_HPP_
