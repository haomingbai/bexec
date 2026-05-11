#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_OPERATION_STATE_HPP_
#define BEXEC_INCLUDE_BEXEC_OPERATION_STATE_HPP_

#include <concepts>
#include <type_traits>

namespace bexec {

/**
 * @brief Starts an operation state by calling op.start().
 */
struct start_t {
  template <class Operation>
    requires(!std::is_rvalue_reference_v<Operation &&> &&
             requires(Operation& operation) {
               { operation.start() } noexcept -> std::same_as<void>;
             })
  constexpr void operator()(Operation&& operation) const noexcept {
    operation.start();
  }
};

inline constexpr start_t start{};

/**
 * @brief Concept for operation states startable through bexec::start.
 */
template <class Operation>
concept operation_state = requires(std::remove_cvref_t<Operation>& operation) {
  bexec::start(operation);
};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_OPERATION_STATE_HPP_
