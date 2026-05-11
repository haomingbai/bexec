#pragma once

#include <atomic>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define BEXEC_EXCEPTIONS_ENABLED 1
#else
#define BEXEC_EXCEPTIONS_ENABLED 0
#endif

/**
 * @file bexec.hpp
 * @brief A small C++20 sender/receiver concurrency library.
 *
 * bexec is intentionally member-customization based. It does not use
 * tag_invoke and it does not depend on stdexec.
 */

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

namespace detail {

template <class>
inline constexpr bool dependent_false = false;

template <class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <class... Lists>
struct concat_type_lists;

template <>
struct concat_type_lists<> {
    using type = type_list<>;
};

template <class... Ts>
struct concat_type_lists<type_list<Ts...>> {
    using type = type_list<Ts...>;
};

template <class... As, class... Bs, class... Rest>
struct concat_type_lists<type_list<As...>, type_list<Bs...>, Rest...> {
    using type = typename concat_type_lists<type_list<As..., Bs...>, Rest...>::type;
};

template <class... Lists>
using concat_type_lists_t = typename concat_type_lists<Lists...>::type;

template <class T, class List>
struct type_list_contains;

template <class T>
struct type_list_contains<T, type_list<>> : std::false_type {};

template <class T, class Head, class... Tail>
struct type_list_contains<T, type_list<Head, Tail...>>
    : std::conditional_t<std::same_as<T, Head>, std::true_type,
                         type_list_contains<T, type_list<Tail...>>> {};

template <class T, class List>
inline constexpr bool type_list_contains_v = type_list_contains<T, List>::value;

template <class Seen, class Input>
struct unique_type_list_impl;

template <class... Seen>
struct unique_type_list_impl<type_list<Seen...>, type_list<>> {
    using type = type_list<Seen...>;
};

template <class... Seen, class Head, class... Tail>
struct unique_type_list_impl<type_list<Seen...>, type_list<Head, Tail...>> {
    using next_seen = std::conditional_t<type_list_contains_v<Head, type_list<Seen...>>,
                                         type_list<Seen...>,
                                         type_list<Seen..., Head>>;
    using type = typename unique_type_list_impl<next_seen, type_list<Tail...>>::type;
};

template <class List>
using unique_type_list_t = typename unique_type_list_impl<type_list<>, List>::type;

template <class List>
struct variant_from_type_list;

template <class... Ts>
struct variant_from_type_list<type_list<Ts...>> {
    using type = std::variant<Ts...>;
};

template <class List>
using variant_from_type_list_t = typename variant_from_type_list<List>::type;

template <class T, class Variant>
struct variant_contains;

template <class T, class... Ts>
struct variant_contains<T, std::variant<Ts...>>
    : std::bool_constant<(std::same_as<T, Ts> || ...)> {};

template <class T, class Variant>
inline constexpr bool variant_contains_v = variant_contains<T, Variant>::value;

template <class Sender>
struct sender_completion_signatures {
    using type = typename remove_cvref_t<Sender>::completion_signatures;
};

template <class Sender>
using sender_completion_signatures_t = typename sender_completion_signatures<Sender>::type;

template <class Sender>
using sender_error_types_t = typename sender_completion_signatures_t<Sender>::error_types;

template <class Sender>
using sender_value_signatures_t = typename sender_completion_signatures_t<Sender>::value_signatures;

template <class Sender>
inline constexpr bool sender_sends_stopped_v =
    sender_completion_signatures_t<Sender>::sends_stopped;

template <class Fn, class Signature>
struct then_value_signature;

template <class Fn, class... Args>
struct then_value_signature<Fn, value_signature<Args...>> {
    using invoke_result = std::invoke_result_t<Fn&, Args...>;
    using type = std::conditional_t<std::is_void_v<invoke_result>,
                                    value_signature<>,
                                    value_signature<std::decay_t<invoke_result>>>;
};

template <class Fn, class ValueSignatures>
struct then_value_signatures;

template <class Fn, class... Signatures>
struct then_value_signatures<Fn, type_list<Signatures...>> {
    using type = type_list<typename then_value_signature<Fn, Signatures>::type...>;
};

template <class Fn, class Sender>
using then_value_signatures_t =
    typename then_value_signatures<Fn, sender_value_signatures_t<Sender>>::type;

template <class Sender>
using sender_errors_with_exception_t =
    unique_type_list_t<concat_type_lists_t<sender_error_types_t<Sender>,
                                           type_list<std::exception_ptr>>>;

struct empty_callback {
    void operator()() const noexcept {}
};

} // namespace detail

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

/**
 * @brief Query tag used to obtain a stop token from an environment.
 */
struct get_stop_token_t {};

/**
 * @brief Query tag used to obtain a scheduler from an environment.
 */
struct get_scheduler_t {};

inline constexpr get_stop_token_t get_stop_token{};
inline constexpr get_scheduler_t get_scheduler{};

/**
 * @brief Member-based query CPO.
 *
 * query(env, tag) calls env.query(tag). No tag_invoke fallback is provided.
 */
struct query_t {
    template <class Env, class QueryTag>
        requires requires(Env&& env, QueryTag tag) {
            std::forward<Env>(env).query(tag);
        }
    constexpr decltype(auto) operator()(Env&& env, QueryTag tag) const
        noexcept(noexcept(std::forward<Env>(env).query(tag))) {
        return std::forward<Env>(env).query(tag);
    }
};

inline constexpr query_t query{};

/**
 * @brief Empty environment that provides a never_stop_token.
 */
struct empty_env {
    /** @brief Returns a token that never requests stop. */
    [[nodiscard]] never_stop_token query(get_stop_token_t) const noexcept {
        return {};
    }
};

/**
 * @brief Environment wrapper that overrides get_stop_token and delegates other queries.
 */
template <class BaseEnv = empty_env>
class env_with_stop_token {
public:
    env_with_stop_token(inplace_stop_token token, BaseEnv base = {})
        : token_(std::move(token)), base_(std::move(base)) {}

    [[nodiscard]] inplace_stop_token query(get_stop_token_t) const noexcept {
        return token_;
    }

    template <class QueryTag>
        requires(!std::same_as<QueryTag, get_stop_token_t> &&
                 requires(const BaseEnv& base, QueryTag tag) { bexec::query(base, tag); })
    decltype(auto) query(QueryTag tag) const
        noexcept(noexcept(bexec::query(base_, tag))) {
        return bexec::query(base_, tag);
    }

private:
    inplace_stop_token token_;
    BaseEnv base_;
};

/**
 * @brief Receiver environment CPO.
 *
 * get_env(receiver) calls receiver.get_env() when available, otherwise returns
 * empty_env.
 */
struct get_env_t {
    template <class Receiver>
    constexpr auto operator()(Receiver&& receiver) const {
        if constexpr (requires { std::forward<Receiver>(receiver).get_env(); }) {
            return std::forward<Receiver>(receiver).get_env();
        } else {
            return empty_env{};
        }
    }
};

inline constexpr get_env_t get_env{};

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

/**
 * @brief Sender produced by just(values...).
 */
template <class... Values>
class just_sender {
public:
    using completion_signatures =
        bexec::completion_signatures<type_list<value_signature<Values...>>, type_list<>, false>;

    explicit just_sender(Values... values)
        : values_(std::move(values)...) {}

    template <class Receiver>
    class operation {
    public:
        operation(std::tuple<Values...> values, Receiver receiver)
            : values_(std::move(values)), receiver_(std::move(receiver)) {}

        /** @brief Completes synchronously with set_value(values...). */
        void start() {
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
        bexec::completion_signatures<type_list<>, type_list<Error>, false>;

    explicit just_error_sender(Error error)
        : error_(std::move(error)) {}

    template <class Receiver>
    class operation {
    public:
        operation(Error error, Receiver receiver)
            : error_(std::move(error)), receiver_(std::move(receiver)) {}

        /** @brief Completes synchronously with set_error(error). */
        void start() {
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
    using completion_signatures = bexec::completion_signatures<type_list<>, type_list<>, true>;

    template <class Receiver>
    class operation {
    public:
        explicit operation(Receiver receiver)
            : receiver_(std::move(receiver)) {}

        /** @brief Completes synchronously with set_stopped(). */
        void start() {
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

template <class Sender, class Fn>
class then_sender;

template <class Fn>
class then_closure {
public:
    explicit then_closure(Fn fn)
        : fn_(std::move(fn)) {}

    template <sender Sender>
    [[nodiscard]] auto operator()(Sender&& sender) const& {
        return then_sender<detail::remove_cvref_t<Sender>, Fn>{
            std::forward<Sender>(sender), fn_};
    }

    template <sender Sender>
    [[nodiscard]] auto operator()(Sender&& sender) && {
        return then_sender<detail::remove_cvref_t<Sender>, Fn>{
            std::forward<Sender>(sender), std::move(fn_)};
    }

private:
    Fn fn_;
};

template <sender Sender, class Fn>
[[nodiscard]] auto operator|(Sender&& sender, then_closure<Fn> closure) {
    return std::move(closure)(std::forward<Sender>(sender));
}

namespace detail {

template <class Receiver, class Fn>
class then_receiver {
public:
    then_receiver(Receiver receiver, Fn fn)
        : receiver_(std::move(receiver)), fn_(std::move(fn)) {}

    [[nodiscard]] auto get_env() noexcept(noexcept(bexec::get_env(receiver_))) {
        return bexec::get_env(receiver_);
    }

    template <class... Args>
    void set_value(Args&&... args) {
#if BEXEC_EXCEPTIONS_ENABLED
        try {
#endif
            complete_value(std::forward<Args>(args)...);
#if BEXEC_EXCEPTIONS_ENABLED
        } catch (...) {
            bexec::set_error(std::move(receiver_), std::current_exception());
        }
#endif
    }

    template <class Error>
    void set_error(Error&& error) noexcept(noexcept(bexec::set_error(
        std::move(receiver_), std::forward<Error>(error)))) {
        bexec::set_error(std::move(receiver_), std::forward<Error>(error));
    }

    void set_stopped() noexcept(noexcept(bexec::set_stopped(std::move(receiver_)))) {
        bexec::set_stopped(std::move(receiver_));
    }

private:
    template <class... Args>
    void complete_value(Args&&... args) {
        if constexpr (std::is_void_v<std::invoke_result_t<Fn&, Args...>>) {
            std::invoke(fn_, std::forward<Args>(args)...);
            bexec::set_value(std::move(receiver_));
        } else {
            bexec::set_value(std::move(receiver_),
                             std::invoke(fn_, std::forward<Args>(args)...));
        }
    }

    Receiver receiver_;
    Fn fn_;
};

template <class Operation>
class pass_through_operation {
public:
    explicit pass_through_operation(Operation operation)
        : operation_(std::move(operation)) {}

    void start() {
        bexec::start(operation_);
    }

private:
    Operation operation_;
};

} // namespace detail

/**
 * @brief Sender adaptor that transforms set_value through a callable.
 */
template <class Sender, class Fn>
class then_sender {
public:
    using completion_signatures = bexec::completion_signatures<
        detail::then_value_signatures_t<Fn, Sender>,
        detail::sender_errors_with_exception_t<Sender>,
        detail::sender_sends_stopped_v<Sender>>;

    then_sender(Sender sender, Fn fn)
        : sender_(std::move(sender)), fn_(std::move(fn)) {}

    template <class Receiver>
    auto connect(Receiver receiver) && {
        auto wrapped = detail::then_receiver<Receiver, Fn>{std::move(receiver), std::move(fn_)};
        auto operation = bexec::connect(std::move(sender_), std::move(wrapped));
        return detail::pass_through_operation<decltype(operation)>{std::move(operation)};
    }

    template <class Receiver>
        requires std::copy_constructible<Sender> && std::copy_constructible<Fn>
    auto connect(Receiver receiver) const& {
        auto wrapped = detail::then_receiver<Receiver, Fn>{std::move(receiver), fn_};
        auto operation = bexec::connect(sender_, std::move(wrapped));
        return detail::pass_through_operation<decltype(operation)>{std::move(operation)};
    }

private:
    Sender sender_;
    Fn fn_;
};

/**
 * @brief Creates a pipeable sender adaptor that transforms set_value.
 */
template <class Fn>
[[nodiscard]] auto then(Fn&& fn) {
    return then_closure<std::decay_t<Fn>>{std::forward<Fn>(fn)};
}

/**
 * @brief Applies then directly to a sender.
 */
template <sender Sender, class Fn>
[[nodiscard]] auto then(Sender&& sender, Fn&& fn) {
    return then_sender<detail::remove_cvref_t<Sender>, std::decay_t<Fn>>{
        std::forward<Sender>(sender), std::forward<Fn>(fn)};
}

class io_context;

/**
 * @brief A simple thread-safe FIFO event loop.
 *
 * run() drains currently queued work until the queue is empty or stop() is
 * requested. enqueue/post are thread-safe; handlers execute on the thread that
 * calls run() or run_one().
 */
class io_context {
public:
    class scheduler;

    io_context() = default;
    io_context(const io_context&) = delete;
    io_context& operator=(const io_context&) = delete;

    /** @brief Returns a scheduler associated with this event loop. */
    [[nodiscard]] scheduler get_scheduler() noexcept;

    /** @brief Enqueues work. Returns false when the context is stopped. */
    bool post(std::function<void()> work) {
        {
            std::lock_guard lock(mutex_);
            if (stopped_) {
                return false;
            }
            queue_.push_back(std::move(work));
        }
        cv_.notify_one();
        return true;
    }

    /** @brief Alias for post(). */
    bool enqueue(std::function<void()> work) {
        return post(std::move(work));
    }

    /** @brief Runs one queued item if available. */
    std::size_t run_one() {
        std::function<void()> work;
        {
            std::lock_guard lock(mutex_);
            if (stopped_ || queue_.empty()) {
                return 0;
            }
            work = std::move(queue_.front());
            queue_.pop_front();
        }
        work();
        return 1;
    }

    /** @brief Drains queued work until empty or stopped. */
    std::size_t run() {
        std::size_t count = 0;
        while (run_one() != 0) {
            ++count;
        }
        return count;
    }

    /** @brief Requests that run()/run_one() stop processing new work. */
    void stop() noexcept {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    /** @brief Clears the stopped flag so new work can be enqueued. */
    void restart() noexcept {
        std::lock_guard lock(mutex_);
        stopped_ = false;
    }

    /** @brief Returns true if stop() has been called and restart() has not. */
    [[nodiscard]] bool stopped() const noexcept {
        std::lock_guard lock(mutex_);
        return stopped_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
    bool stopped_{false};
};

namespace detail {

class schedule_sender {
public:
    using completion_signatures =
        bexec::completion_signatures<type_list<value_signature<>>,
                                      type_list<std::exception_ptr>,
                                      true>;

    explicit schedule_sender(io_context& context)
        : context_(&context) {}

    template <class Receiver>
    class operation {
    public:
        operation(io_context& context, Receiver receiver)
            : context_(&context), receiver_(std::move(receiver)) {}

        void start() {
            auto token = bexec::query(bexec::get_env(*receiver_), bexec::get_stop_token);
            if (token.stop_requested()) {
                bexec::set_stopped(std::move(*receiver_));
                receiver_.reset();
                return;
            }

#if BEXEC_EXCEPTIONS_ENABLED
            try {
#endif
                auto receiver = std::make_shared<Receiver>(std::move(*receiver_));
                receiver_.reset();

                const bool queued = context_->post([receiver]() mutable {
                    auto inner_token =
                        bexec::query(bexec::get_env(*receiver), bexec::get_stop_token);
                    if (inner_token.stop_requested()) {
                        bexec::set_stopped(std::move(*receiver));
                    } else {
                        bexec::set_value(std::move(*receiver));
                    }
                });

                if (!queued) {
                    bexec::set_stopped(std::move(*receiver));
                }
#if BEXEC_EXCEPTIONS_ENABLED
            } catch (...) {
                bexec::set_error(std::move(*receiver_), std::current_exception());
                receiver_.reset();
            }
#endif
        }

    private:
        io_context* context_;
        std::optional<Receiver> receiver_;
    };

    template <class Receiver>
    auto connect(Receiver receiver) const {
        return operation<Receiver>{*context_, std::move(receiver)};
    }

private:
    io_context* context_;
};

} // namespace detail

/**
 * @brief Scheduler handle for io_context.
 */
class io_context::scheduler {
public:
    scheduler() = default;

    /** @brief Returns a sender that completes on the associated io_context. */
    [[nodiscard]] detail::schedule_sender schedule() const {
        return detail::schedule_sender{*context_};
    }

    /** @brief Enqueues arbitrary work on the associated io_context. */
    bool post(std::function<void()> work) const {
        return context_->post(std::move(work));
    }

    /** @brief Awaiter that resumes a coroutine on the associated io_context. */
    class schedule_awaiter {
    public:
        explicit schedule_awaiter(scheduler sched)
            : context_(sched.context_) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) const {
            if (!context_->post([handle]() mutable { handle.resume(); })) {
                handle.resume();
            }
        }

        void await_resume() const noexcept {}

    private:
        io_context* context_;
    };

    /** @brief Returns an awaitable that resumes on this scheduler. */
    [[nodiscard]] schedule_awaiter schedule_awaitable() const {
        return schedule_awaiter{*this};
    }

    friend bool operator==(scheduler lhs, scheduler rhs) noexcept {
        return lhs.context_ == rhs.context_;
    }

private:
    friend class io_context;

    explicit scheduler(io_context& context)
        : context_(&context) {}

    io_context* context_{nullptr};
};

inline io_context::scheduler io_context::get_scheduler() noexcept {
    return scheduler{*this};
}

/**
 * @brief Coroutine task type used by bexec scheduler examples and tests.
 *
 * The task is lazy: call start() to run until the first suspension. result()
 * consumes the stored result after done() is true.
 */
template <class T>
class task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::optional<T> value;
        std::exception_ptr error;

        task get_return_object() {
            return task{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        template <class U>
        void return_value(U&& result) {
            value.emplace(std::forward<U>(result));
        }

        void unhandled_exception() noexcept {
#if BEXEC_EXCEPTIONS_ENABLED
            error = std::current_exception();
#else
            std::terminate();
#endif
        }
    };

    task() = default;
    explicit task(handle_type handle)
        : handle_(handle) {}

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    task(task&& other) noexcept
        : handle_(std::exchange(other.handle_, {})) {}

    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    ~task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    /** @brief Starts or resumes the coroutine. */
    void start() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    /** @brief Returns true when the coroutine reached final suspend. */
    [[nodiscard]] bool done() const noexcept {
        return !handle_ || handle_.done();
    }

    /** @brief Returns the result, rethrowing any stored exception. */
    T result() {
#if BEXEC_EXCEPTIONS_ENABLED
        if (handle_.promise().error) {
            std::rethrow_exception(handle_.promise().error);
        }
#endif
        return std::move(*handle_.promise().value);
    }

private:
    handle_type handle_{};
};

/**
 * @brief Void specialization of task.
 */
template <>
class task<void> {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::exception_ptr error;

        task get_return_object() {
            return task{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}

        void unhandled_exception() noexcept {
#if BEXEC_EXCEPTIONS_ENABLED
            error = std::current_exception();
#else
            std::terminate();
#endif
        }
    };

    task() = default;
    explicit task(handle_type handle)
        : handle_(handle) {}

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    task(task&& other) noexcept
        : handle_(std::exchange(other.handle_, {})) {}

    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    ~task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    /** @brief Starts or resumes the coroutine. */
    void start() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    /** @brief Returns true when the coroutine reached final suspend. */
    [[nodiscard]] bool done() const noexcept {
        return !handle_ || handle_.done();
    }

    /** @brief Rethrows any stored exception. */
    void result() {
#if BEXEC_EXCEPTIONS_ENABLED
        if (handle_.promise().error) {
            std::rethrow_exception(handle_.promise().error);
        }
#endif
    }

private:
    handle_type handle_{};
};

namespace detail {

template <class Factory, class Predicate, class Receiver>
class repeat_until_operation;

template <class Factory, class Predicate, class Receiver>
class repeat_until_child_receiver {
public:
    using parent_type = repeat_until_operation<Factory, Predicate, Receiver>;

    explicit repeat_until_child_receiver(parent_type& parent)
        : parent_(&parent) {}

    [[nodiscard]] auto get_env() noexcept {
        return bexec::get_env(parent_->receiver());
    }

    template <class... Args>
    void set_value(Args&&...) noexcept(noexcept(parent_->child_value())) {
        parent_->child_value();
    }

    template <class Error>
    void set_error(Error&& error) noexcept(noexcept(parent_->child_error(std::forward<Error>(error)))) {
        parent_->child_error(std::forward<Error>(error));
    }

    void set_stopped() noexcept(noexcept(parent_->child_stopped())) {
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

    void start() {
        continue_requested_ = true;
        drain();
    }

    void child_value() {
        child_pending_ = false;

#if BEXEC_EXCEPTIONS_ENABLED
        try {
#endif
            if (predicate_()) {
                done_ = true;
                bexec::set_value(std::move(receiver_));
            } else {
                continue_requested_ = true;
            }
#if BEXEC_EXCEPTIONS_ENABLED
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
    void child_error(Error&& error) {
        child_pending_ = false;
        done_ = true;
        bexec::set_error(std::move(receiver_), std::forward<Error>(error));
    }

    void child_stopped() {
        child_pending_ = false;
        done_ = true;
        bexec::set_stopped(std::move(receiver_));
    }

private:
    void drain() {
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

#if BEXEC_EXCEPTIONS_ENABLED
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
#if BEXEC_EXCEPTIONS_ENABLED
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

} // namespace detail

/**
 * @brief Sender that repeats a sender-producing callable until a predicate succeeds.
 *
 * Child sender values are discarded. This design intentionally uses a factory
 * callable so every iteration receives a fresh sender and operation state.
 */
template <class Factory, class Predicate>
class repeat_until_sender {
public:
    using sender_type = std::invoke_result_t<Factory&>;
    using completion_signatures = bexec::completion_signatures<
        type_list<value_signature<>>,
        detail::sender_errors_with_exception_t<sender_type>,
        true>;

    repeat_until_sender(Factory factory, Predicate predicate)
        : factory_(std::move(factory)), predicate_(std::move(predicate)) {}

    template <class Receiver>
    auto connect(Receiver receiver) && {
        return detail::repeat_until_operation<Factory, Predicate, Receiver>{
            std::move(factory_), std::move(predicate_), std::move(receiver)};
    }

    template <class Receiver>
        requires std::copy_constructible<Factory> && std::copy_constructible<Predicate>
    auto connect(Receiver receiver) const& {
        return detail::repeat_until_operation<Factory, Predicate, Receiver>{
            factory_, predicate_, std::move(receiver)};
    }

private:
    Factory factory_;
    Predicate predicate_;
};

/**
 * @brief Repeats factory() until predicate() returns true.
 */
template <class Factory, class Predicate>
[[nodiscard]] auto repeat_until(Factory&& factory, Predicate&& predicate) {
    return repeat_until_sender<std::decay_t<Factory>, std::decay_t<Predicate>>{
        std::forward<Factory>(factory), std::forward<Predicate>(predicate)};
}

namespace detail {

template <std::size_t Index, class State>
class when_all_child_receiver {
public:
    explicit when_all_child_receiver(std::shared_ptr<State> state)
        : state_(std::move(state)) {}

    [[nodiscard]] auto get_env() {
        return env_with_stop_token{state_->stop_source.get_token(), bexec::get_env(state_->receiver)};
    }

    template <class... Args>
    void set_value(Args&&...) {
        state_->child_value();
    }

    template <class Error>
    void set_error(Error&& error) {
        state_->child_error(std::forward<Error>(error));
    }

    void set_stopped() {
        state_->child_stopped();
    }

private:
    std::shared_ptr<State> state_;
};

template <class Receiver, class ErrorVariant>
struct when_all_state {
    explicit when_all_state(Receiver recv, std::size_t count)
        : receiver(std::move(recv)), remaining(count) {}

    void child_value() {
        finish_one();
    }

    template <class Error>
    void child_error(Error&& error) {
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

    void child_stopped() {
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

    void finish_one() {
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

    void complete_empty() {
        bexec::set_value(std::move(receiver));
    }

    template <class Error>
    void store_error(Error&& error_value) {
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

} // namespace detail

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
        bexec::completion_signatures<type_list<value_signature<>>,
                                      detail::when_all_error_list_t<Senders...>,
                                      true>;

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

        void start() {
            if constexpr (sizeof...(Senders) == 0) {
                state_->complete_empty();
            } else {
                start_all(indices{});
            }
        }

    private:
        template <std::size_t... Indices>
        void start_all(std::index_sequence<Indices...>) {
            (start_one<Indices>(), ...);
        }

        template <std::size_t Index>
        void start_one() {
            using child_receiver =
                detail::when_all_child_receiver<Index, state_type>;
            auto& slot = std::get<Index>(operations_);

#if BEXEC_EXCEPTIONS_ENABLED
            try {
#endif
                slot.emplace(bexec::connect(std::move(std::get<Index>(senders_)),
                                            child_receiver{state_}));
                bexec::start(*slot);
#if BEXEC_EXCEPTIONS_ENABLED
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

#undef BEXEC_EXCEPTIONS_ENABLED
