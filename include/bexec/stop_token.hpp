#pragma once

#include <bexec/detail/type_traits.hpp>

#include <atomic>
#include <concepts>
#include <exception>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace bexec {

/**
 * @brief A stop token that can never request cancellation.
 */
class never_stop_token {
public:
    template <class Callback>
    class callback_type {
    public:
        callback_type(const never_stop_token&, Callback) noexcept {}
    };

    /** @brief Always returns false. */
    [[nodiscard]] bool stop_requested() const noexcept { return false; }
};

namespace detail {

struct stop_callback_record {
    std::atomic_bool active{true};

    virtual ~stop_callback_record() = default;
    virtual void invoke() noexcept = 0;

    void try_invoke() noexcept {
        if (active.exchange(false, std::memory_order_acq_rel)) {
            invoke();
        }
    }
};

template <class Callback>
struct stop_callback_record_impl final : stop_callback_record {
    explicit stop_callback_record_impl(Callback callback)
        : callback_(std::move(callback)) {}

    void invoke() noexcept override {
        try {
            callback_();
        } catch (...) {
            std::terminate();
        }
    }

    Callback callback_;
};

struct stop_state {
    mutable std::mutex mutex;
    bool requested{false};
    std::vector<std::shared_ptr<stop_callback_record>> callbacks;
};

} // namespace detail

class inplace_stop_source;
class inplace_stop_token;

/**
 * @brief Registration object for callbacks attached to inplace_stop_token.
 *
 * Destroying the registration prevents future invocation if cancellation has
 * not already reached the callback. Callback invocation is one-shot.
 */
template <class Callback>
class inplace_stop_callback {
public:
    inplace_stop_callback(const inplace_stop_token& token, Callback callback);

    inplace_stop_callback(const inplace_stop_callback&) = delete;
    inplace_stop_callback& operator=(const inplace_stop_callback&) = delete;

    inplace_stop_callback(inplace_stop_callback&&) = delete;
    inplace_stop_callback& operator=(inplace_stop_callback&&) = delete;

    ~inplace_stop_callback() {
        if (record_) {
            record_->active.store(false, std::memory_order_release);
        }
    }

private:
    std::shared_ptr<detail::stop_callback_record> record_;
};

/**
 * @brief A lightweight copyable stop token associated with inplace_stop_source.
 */
class inplace_stop_token {
public:
    template <class Callback>
    using callback_type = inplace_stop_callback<std::decay_t<Callback>>;

    inplace_stop_token() = default;

    /** @brief Returns true once the associated source has requested stop. */
    [[nodiscard]] bool stop_requested() const noexcept {
        auto state = state_.lock();
        if (!state) {
            return false;
        }
        std::lock_guard lock(state->mutex);
        return state->requested;
    }

private:
    friend class inplace_stop_source;
    template <class Callback>
    friend class inplace_stop_callback;

    explicit inplace_stop_token(std::weak_ptr<detail::stop_state> state)
        : state_(std::move(state)) {}

    std::weak_ptr<detail::stop_state> state_;
};

/**
 * @brief A small in-place cancellation source.
 *
 * request_stop() is thread-safe. Registered callbacks either run when stop is
 * requested or, if stop was already requested, during registration.
 */
class inplace_stop_source {
public:
    inplace_stop_source()
        : state_(std::make_shared<detail::stop_state>()) {}

    /** @brief Returns a token connected to this source. */
    [[nodiscard]] inplace_stop_token get_token() const noexcept {
        return inplace_stop_token{state_};
    }

    /** @brief Returns whether stop has already been requested. */
    [[nodiscard]] bool stop_requested() const noexcept {
        std::lock_guard lock(state_->mutex);
        return state_->requested;
    }

    /**
     * @brief Requests stop and invokes registered callbacks once.
     * @return true if this call made the stop request, false if it was already requested.
     */
    bool request_stop() noexcept {
        std::vector<std::shared_ptr<detail::stop_callback_record>> callbacks;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->requested) {
                return false;
            }
            state_->requested = true;
            callbacks = state_->callbacks;
            state_->callbacks.clear();
        }

        for (auto& callback : callbacks) {
            callback->try_invoke();
        }
        return true;
    }

private:
    std::shared_ptr<detail::stop_state> state_;
};

template <class Callback>
inplace_stop_callback<Callback>::inplace_stop_callback(const inplace_stop_token& token,
                                                       Callback callback)
    : record_(std::make_shared<detail::stop_callback_record_impl<std::decay_t<Callback>>>(
          std::move(callback))) {
    auto state = token.state_.lock();
    if (!state) {
        record_->active.store(false, std::memory_order_release);
        return;
    }

    bool fire_now = false;
    {
        std::lock_guard lock(state->mutex);
        if (state->requested) {
            fire_now = true;
        } else {
            state->callbacks.push_back(record_);
        }
    }

    if (fire_now) {
        record_->try_invoke();
    }
}

/**
 * @brief Concept for stop-token-like types used by bexec.
 */
template <class Token>
concept stop_token =
    std::copy_constructible<Token> &&
    requires(const Token& token) {
        { token.stop_requested() } -> std::same_as<bool>;
        typename Token::template callback_type<detail::empty_callback>;
    };

/**
 * @brief Concept for stop-source-like types used by bexec.
 */
template <class Source>
concept stop_source =
    requires(Source& source) {
        { source.request_stop() } -> std::same_as<bool>;
        { source.stop_requested() } -> std::same_as<bool>;
        { source.get_token() } -> stop_token;
    };

} // namespace bexec
