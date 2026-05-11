#pragma once

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

    explicit just_sender(Values... values)
        : values_(std::move(values)...) {}

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
 * @brief Creates a sender that completes synchronously with set_value(values...).
 */
template <class... Values>
[[nodiscard]] auto just(Values&&... values) {
    return just_sender<std::decay_t<Values>...>{std::forward<Values>(values)...};
}

/**
 * @brief Sender produced by just_error(error).
 */
template <class Error>
class just_error_sender {
public:
    using completion_signatures =
        bexec::completion_signatures<set_error_t(Error)>;

    explicit just_error_sender(Error error)
        : error_(std::move(error)) {}

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
 * @brief Creates a sender that completes synchronously with set_error(error).
 */
template <class Error>
[[nodiscard]] auto just_error(Error&& error) {
    return just_error_sender<std::decay_t<Error>>{std::forward<Error>(error)};
}

/**
 * @brief Sender produced by just_stopped().
 */
class just_stopped_sender {
public:
    using completion_signatures = bexec::completion_signatures<set_stopped_t()>;

    template <class Receiver>
    class operation {
    public:
        explicit operation(Receiver receiver)
            : receiver_(std::move(receiver)) {}

        /** @brief Completes synchronously with set_stopped(). */
        void start() noexcept {
            bexec::set_stopped(std::move(receiver_));
        }

    private:
        Receiver receiver_;
    };

    template <class Receiver>
    auto connect(Receiver receiver) const {
        return operation<Receiver>{std::move(receiver)};
    }
};

/**
 * @brief Creates a sender that completes synchronously with set_stopped().
 */
[[nodiscard]] inline just_stopped_sender just_stopped() {
    return {};
}

} // namespace bexec
