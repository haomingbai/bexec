#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_REPEAT_UNTIL_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_REPEAT_UNTIL_HPP_

#include <bexec/detail/config.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/sender.hpp>

#include <exception>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace bexec::detail {

template <class Factory, class Predicate, class Receiver>
class repeat_until_operation;

template <class Factory, class Predicate, class Receiver>
class repeat_until_child_receiver {
public:
    using parent_type = repeat_until_operation<Factory, Predicate, Receiver>;

    explicit repeat_until_child_receiver(parent_type& parent)
        : parent_(&parent) {}

    [[nodiscard]] auto get_env() const noexcept {
        return bexec::get_env(parent_->receiver());
    }

    template <class... Args>
    void set_value(Args&&...) noexcept {
        parent_->child_value();
    }

    template <class Error>
    void set_error(Error&& error) noexcept {
        parent_->child_error(std::forward<Error>(error));
    }

    void set_stopped() noexcept {
        parent_->child_stopped();
    }

private:
    parent_type* parent_;
};

template <class Factory, class Predicate, class Receiver>
class repeat_until_operation {
public:
    using sender_type = std::invoke_result_t<Factory&>;
    using child_receiver_type = repeat_until_child_receiver<Factory, Predicate, Receiver>;
    using child_operation_type =
        decltype(bexec::connect(std::declval<sender_type>(), std::declval<child_receiver_type>()));

    repeat_until_operation(Factory factory, Predicate predicate, Receiver receiver)
        : factory_(std::move(factory)),
          predicate_(std::move(predicate)),
          receiver_(std::move(receiver)) {}

    Receiver& receiver() noexcept { return receiver_; }

    void start() noexcept {
        continue_requested_ = true;
        drain();
    }

    void child_value() noexcept {
        child_pending_ = false;

#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
        try {
#endif
            if (predicate_()) {
                done_ = true;
                bexec::set_value(std::move(receiver_));
            } else {
                continue_requested_ = true;
            }
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
        } catch (...) {
            done_ = true;
            bexec::set_error(std::move(receiver_), std::current_exception());
        }
#endif

        if (!draining_ && continue_requested_ && !done_) {
            drain();
        }
    }

    template <class Error>
    void child_error(Error&& error) noexcept {
        child_pending_ = false;
        done_ = true;
        bexec::set_error(std::move(receiver_), std::forward<Error>(error));
    }

    void child_stopped() noexcept {
        child_pending_ = false;
        done_ = true;
        bexec::set_stopped(std::move(receiver_));
    }

private:
    void drain() noexcept {
        if (draining_) {
            return;
        }

        draining_ = true;

        while (continue_requested_ && !done_) {
            continue_requested_ = false;
            current_.reset();

            auto token = bexec::query(bexec::get_env(receiver_), bexec::get_stop_token);
            if (token.stop_requested()) {
                done_ = true;
                bexec::set_stopped(std::move(receiver_));
                break;
            }

#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
            try {
#endif
                auto sender = factory_();
                current_.emplace(bexec::connect(std::move(sender), child_receiver_type{*this}));
                child_pending_ = true;

                /*
                 * Synchronous senders complete inside start() and set
                 * child_pending_ back to false. The while loop then continues
                 * without recursive start() calls. Asynchronous senders return
                 * with child_pending_ still true; their completion callback
                 * re-enters drain() later if another iteration is needed.
                 */
                bexec::start(*current_);

                if (child_pending_) {
                    break;
                }
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
            } catch (...) {
                done_ = true;
                bexec::set_error(std::move(receiver_), std::current_exception());
                break;
            }
#endif
        }

        draining_ = false;
    }

    Factory factory_;
    Predicate predicate_;
    Receiver receiver_;
    std::optional<child_operation_type> current_;
    bool draining_{false};
    bool child_pending_{false};
    bool continue_requested_{false};
    bool done_{false};
};

} // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_REPEAT_UNTIL_HPP_
