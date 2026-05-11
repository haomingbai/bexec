#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_WHEN_ALL_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_WHEN_ALL_HPP_

#include <bexec/detail/config.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/receiver.hpp>

#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace bexec::detail {

template <std::size_t Index, class State>
class when_all_child_receiver {
public:
    explicit when_all_child_receiver(std::shared_ptr<State> state)
        : state_(std::move(state)) {}

    [[nodiscard]] auto get_env() const noexcept {
        return env_with_stop_token{state_->stop_source.get_token(), bexec::get_env(state_->receiver)};
    }

    template <class... Args>
    void set_value(Args&&...) noexcept {
        state_->child_value();
    }

    template <class Error>
    void set_error(Error&& error) noexcept {
        state_->child_error(std::forward<Error>(error));
    }

    void set_stopped() noexcept {
        state_->child_stopped();
    }

private:
    std::shared_ptr<State> state_;
};

template <class Receiver, class ErrorVariant>
struct when_all_state {
    explicit when_all_state(Receiver recv, std::size_t count)
        : receiver(std::move(recv)), remaining(count) {}

    void child_value() noexcept {
        finish_one();
    }

    template <class Error>
    void child_error(Error&& error) noexcept {
        bool request_stop = false;
        {
            std::lock_guard lock(mutex);
            if (terminal == terminal_kind::none) {
                terminal = terminal_kind::error;
                store_error(std::forward<Error>(error));
                request_stop = true;
            }
        }
        if (request_stop) {
            stop_source.request_stop();
        }
        finish_one();
    }

    void child_stopped() noexcept {
        bool request_stop = false;
        {
            std::lock_guard lock(mutex);
            if (terminal == terminal_kind::none) {
                terminal = terminal_kind::stopped;
                request_stop = true;
            }
        }
        if (request_stop) {
            stop_source.request_stop();
        }
        finish_one();
    }

    void finish_one() noexcept {
        std::optional<ErrorVariant> error_to_deliver;
        std::optional<Receiver> receiver_to_complete;
        terminal_kind final_terminal = terminal_kind::none;

        {
            std::lock_guard lock(mutex);
            if (remaining == 0) {
                return;
            }
            --remaining;
            if (remaining != 0 || completed) {
                return;
            }

            completed = true;
            final_terminal = terminal;
            if (error) {
                error_to_deliver.emplace(std::move(*error));
            }
            receiver_to_complete.emplace(std::move(receiver));
        }

        if (final_terminal == terminal_kind::error) {
            bexec::set_error(std::move(*receiver_to_complete), std::move(*error_to_deliver));
        } else if (final_terminal == terminal_kind::stopped) {
            bexec::set_stopped(std::move(*receiver_to_complete));
        } else {
            bexec::set_value(std::move(*receiver_to_complete));
        }
    }

    void complete_empty() noexcept {
        bexec::set_value(std::move(receiver));
    }

    template <class Error>
    void store_error(Error&& error_value) noexcept {
        using error_type = std::decay_t<Error>;
        if constexpr (variant_contains_v<error_type, ErrorVariant>) {
            error.emplace(std::in_place_type<error_type>, std::forward<Error>(error_value));
        } else {
            static_assert(dependent_false<Error>,
                          "when_all child sent an error type not listed in completion signatures");
        }
    }

    enum class terminal_kind { none, error, stopped };

    Receiver receiver;
    std::size_t remaining;
    std::mutex mutex;
    inplace_stop_source stop_source;
    terminal_kind terminal{terminal_kind::none};
    std::optional<ErrorVariant> error;
    bool completed{false};
};

template <class Receiver, class ErrorVariant, class SenderTuple, std::size_t Index>
using when_all_child_operation_t = decltype(bexec::connect(
    std::declval<std::tuple_element_t<Index, SenderTuple>>(),
    std::declval<when_all_child_receiver<
        Index, when_all_state<Receiver, ErrorVariant>>>()));

template <class Receiver, class ErrorVariant, class SenderTuple, class Indices>
struct when_all_operation_tuple;

template <class Receiver, class ErrorVariant, class SenderTuple, std::size_t... Indices>
struct when_all_operation_tuple<Receiver, ErrorVariant, SenderTuple, std::index_sequence<Indices...>> {
    using type = std::tuple<
        std::optional<when_all_child_operation_t<Receiver, ErrorVariant, SenderTuple, Indices>>...>;
};

template <class... Senders>
using when_all_error_list_t = unique_type_list_t<concat_type_lists_t<
    sender_error_types_t<Senders>...,
    type_list<std::exception_ptr>>>;

template <class... Senders>
using when_all_error_variant_t = variant_from_type_list_t<when_all_error_list_t<Senders...>>;

} // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_WHEN_ALL_HPP_
