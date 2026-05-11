#pragma once

#include <bexec/cpo.hpp>
#include <bexec/detail/type_traits.hpp>

#include <concepts>
#include <exception>
#include <utility>

namespace bexec {

/**
 * @brief Concept for operation states startable through bexec::start.
 */
template <class Operation>
concept operation_state = requires(detail::remove_cvref_t<Operation>& operation) {
    bexec::start(operation);
};

/**
 * @brief Concept for receivers that accept value, error, and stopped signals.
 */
template <class Receiver, class... Args>
concept receiver_of =
    requires(Receiver&& receiver, Args&&... args) {
        bexec::set_value(std::forward<Receiver>(receiver), std::forward<Args>(args)...);
        bexec::set_error(std::forward<Receiver>(receiver), std::exception_ptr{});
        bexec::set_stopped(std::forward<Receiver>(receiver));
    };

/**
 * @brief Concept for sender-like types that publish MVP completion metadata.
 */
template <class Sender>
concept sender =
    std::move_constructible<detail::remove_cvref_t<Sender>> &&
    requires { typename detail::remove_cvref_t<Sender>::completion_signatures; };

/**
 * @brief Concept for sender/receiver pairs connectable through bexec::connect.
 */
template <class Sender, class Receiver>
concept sender_to =
    sender<Sender> &&
    requires(Sender&& sender, Receiver&& receiver) {
        { bexec::connect(std::forward<Sender>(sender), std::forward<Receiver>(receiver)) }
            -> operation_state;
    };

/**
 * @brief Concept for scheduler-like types that provide schedule().
 */
template <class Scheduler>
concept scheduler =
    requires(Scheduler&& sched) {
        { bexec::schedule(std::forward<Scheduler>(sched)) } -> sender;
    };

} // namespace bexec
