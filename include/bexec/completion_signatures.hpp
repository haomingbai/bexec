#pragma once

#include <exception>

namespace bexec {

template <class... Ts>
struct type_list {};

/**
 * @brief Describes one set_value completion signature.
 */
template <class... Ts>
struct value_signature {};

/**
 * @brief Minimal completion signature metadata used by this MVP.
 *
 * The type is intentionally smaller than P2300 completion_signatures. It is
 * sufficient for simple value-type discovery, when_all error aggregation, and
 * stopped awareness.
 */
template <class ValueSignatures = type_list<value_signature<>>,
          class ErrorTypes = type_list<std::exception_ptr>,
          bool SendsStopped = false>
struct completion_signatures {
    using value_signatures = ValueSignatures;
    using error_types = ErrorTypes;
    static constexpr bool sends_stopped = SendsStopped;
};

} // namespace bexec
