#pragma once

#include <utility>

namespace bexec {

/**
 * @brief Starts an operation state by calling op.start().
 */
struct start_t {
    template <class Operation>
        requires requires(Operation& operation) { operation.start(); }
    constexpr void operator()(Operation& operation) const
        noexcept(noexcept(operation.start())) {
        operation.start();
    }
};

inline constexpr start_t start{};

/**
 * @brief Delivers a value completion by calling receiver.set_value(args...).
 */
struct set_value_t {
    template <class Receiver, class... Args>
        requires requires(Receiver&& receiver, Args&&... args) {
            std::forward<Receiver>(receiver).set_value(std::forward<Args>(args)...);
        }
    constexpr decltype(auto) operator()(Receiver&& receiver, Args&&... args) const
        noexcept(noexcept(std::forward<Receiver>(receiver).set_value(
            std::forward<Args>(args)...))) {
        return std::forward<Receiver>(receiver).set_value(std::forward<Args>(args)...);
    }
};

inline constexpr set_value_t set_value{};

/**
 * @brief Delivers an error completion by calling receiver.set_error(error).
 */
struct set_error_t {
    template <class Receiver, class Error>
        requires requires(Receiver&& receiver, Error&& error) {
            std::forward<Receiver>(receiver).set_error(std::forward<Error>(error));
        }
    constexpr decltype(auto) operator()(Receiver&& receiver, Error&& error) const
        noexcept(noexcept(std::forward<Receiver>(receiver).set_error(
            std::forward<Error>(error)))) {
        return std::forward<Receiver>(receiver).set_error(std::forward<Error>(error));
    }
};

inline constexpr set_error_t set_error{};

/**
 * @brief Delivers stopped completion by calling receiver.set_stopped().
 */
struct set_stopped_t {
    template <class Receiver>
        requires requires(Receiver&& receiver) {
            std::forward<Receiver>(receiver).set_stopped();
        }
    constexpr decltype(auto) operator()(Receiver&& receiver) const
        noexcept(noexcept(std::forward<Receiver>(receiver).set_stopped())) {
        return std::forward<Receiver>(receiver).set_stopped();
    }
};

inline constexpr set_stopped_t set_stopped{};

/**
 * @brief Connects a sender to a receiver by calling sender.connect(receiver).
 */
struct connect_t {
    template <class Sender, class Receiver>
        requires requires(Sender&& sender, Receiver&& receiver) {
            std::forward<Sender>(sender).connect(std::forward<Receiver>(receiver));
        }
    constexpr decltype(auto) operator()(Sender&& sender, Receiver&& receiver) const
        noexcept(noexcept(std::forward<Sender>(sender).connect(
            std::forward<Receiver>(receiver)))) {
        return std::forward<Sender>(sender).connect(std::forward<Receiver>(receiver));
    }
};

inline constexpr connect_t connect{};

/**
 * @brief Obtains a scheduling sender by calling scheduler.schedule().
 */
struct schedule_t {
    template <class Scheduler>
        requires requires(Scheduler&& scheduler) {
            std::forward<Scheduler>(scheduler).schedule();
        }
    constexpr decltype(auto) operator()(Scheduler&& scheduler) const
        noexcept(noexcept(std::forward<Scheduler>(scheduler).schedule())) {
        return std::forward<Scheduler>(scheduler).schedule();
    }
};

inline constexpr schedule_t schedule{};

} // namespace bexec
