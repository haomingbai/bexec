#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_WHEN_ALL_HPP_
#define BEXEC_INCLUDE_BEXEC_WHEN_ALL_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/concepts.hpp>
#include <bexec/detail/config.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/detail/when_all.hpp>
#include <bexec/receiver.hpp>

#include <concepts>
#include <exception>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace bexec {

/**
 * @brief Sender that waits for all child senders.
 *
 * Successful child values are currently discarded; all-success completion sends
 * set_value() with no values. The first error or stopped signal determines the
 * final terminal signal, but completion is delayed until all started children
 * have finished.
 */
template <class... Senders>
class when_all_sender {
public:
    using error_variant = detail::when_all_error_variant_t<Senders...>;
    using completion_signatures =
        bexec::completion_signatures<set_value_t(),
                                      set_error_t(error_variant),
                                      set_stopped_t()>;

    explicit when_all_sender(Senders... senders)
        : senders_(std::move(senders)...) {}

    template <class Receiver>
    class operation {
    public:
        using sender_tuple = std::tuple<Senders...>;
        using state_type = detail::when_all_state<Receiver, error_variant>;
        using indices = std::index_sequence_for<Senders...>;
        using operation_tuple =
            typename detail::when_all_operation_tuple<Receiver, error_variant, sender_tuple, indices>::type;

        operation(sender_tuple senders, Receiver receiver)
            : senders_(std::move(senders)),
              state_(std::make_shared<state_type>(std::move(receiver), sizeof...(Senders))) {}

        void start() noexcept {
            if constexpr (sizeof...(Senders) == 0) {
                state_->complete_empty();
            } else {
                start_all(indices{});
            }
        }

    private:
        template <std::size_t... Indices>
        void start_all(std::index_sequence<Indices...>) noexcept {
            (start_one<Indices>(), ...);
        }

        template <std::size_t Index>
        void start_one() noexcept {
            using child_receiver =
                detail::when_all_child_receiver<Index, state_type>;
            auto& slot = std::get<Index>(operations_);

#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
            try {
#endif
                slot.emplace(bexec::connect(std::move(std::get<Index>(senders_)),
                                            child_receiver{state_}));
                bexec::start(*slot);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
            } catch (...) {
                state_->child_error(std::current_exception());
            }
#endif
        }

        sender_tuple senders_;
        std::shared_ptr<state_type> state_;
        operation_tuple operations_;
    };

    template <class Receiver>
    auto connect(Receiver receiver) && {
        return operation<Receiver>{std::move(senders_), std::move(receiver)};
    }

    template <class Receiver>
        requires((std::copy_constructible<Senders> && ...))
    auto connect(Receiver receiver) const& {
        return operation<Receiver>{senders_, std::move(receiver)};
    }

private:
    std::tuple<Senders...> senders_;
};

/**
 * @brief Starts all child senders and completes after all have finished.
 */
template <sender... Senders>
[[nodiscard]] auto when_all(Senders&&... senders) {
    return when_all_sender<detail::remove_cvref_t<Senders>...>{
        std::forward<Senders>(senders)...};
}

} // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_WHEN_ALL_HPP_
