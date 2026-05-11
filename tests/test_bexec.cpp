#include <bexec/bexec.hpp>

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace {

int failures = 0;

#define CHECK(EXPR)                                                                    \
    do {                                                                               \
        if (!(EXPR)) {                                                                 \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #EXPR      \
                      << '\n';                                                         \
            ++failures;                                                                \
        }                                                                              \
    } while (false)

enum class signal_kind { none, value, error, stopped };

struct shared_state {
    signal_kind signal{signal_kind::none};
    int int_value{0};
    std::string string_value;
    std::exception_ptr exception;
};

struct any_receiver {
    std::shared_ptr<shared_state> state = std::make_shared<shared_state>();

    void set_value() {
        state->signal = signal_kind::value;
    }

    void set_value(int value) {
        state->signal = signal_kind::value;
        state->int_value = value;
    }

    void set_value(std::unique_ptr<int> value) {
        state->signal = signal_kind::value;
        state->int_value = *value;
    }

    template <class Error>
    void set_error(Error&& error) {
        state->signal = signal_kind::error;
        if constexpr (std::same_as<std::decay_t<Error>, std::exception_ptr>) {
            state->exception = std::forward<Error>(error);
        }
    }

    void set_stopped() {
        state->signal = signal_kind::stopped;
    }
};

template <class Env>
struct env_receiver {
    std::shared_ptr<shared_state> state = std::make_shared<shared_state>();
    Env env;

    void set_value() {
        state->signal = signal_kind::value;
    }

    template <class Error>
    void set_error(Error&&) {
        state->signal = signal_kind::error;
    }

    void set_stopped() {
        state->signal = signal_kind::stopped;
    }

    Env get_env() const {
        return env;
    }
};

template <class Variant>
struct variant_receiver {
    std::shared_ptr<shared_state> state = std::make_shared<shared_state>();
    std::shared_ptr<std::optional<Variant>> error = std::make_shared<std::optional<Variant>>();

    void set_value() {
        state->signal = signal_kind::value;
    }

    void set_error(Variant value) {
        state->signal = signal_kind::error;
        *error = std::move(value);
    }

    void set_stopped() {
        state->signal = signal_kind::stopped;
    }
};

void test_concepts_and_just_then() {
    static_assert(bexec::sender<decltype(bexec::just(1))>);

    auto state = std::make_shared<shared_state>();
    any_receiver receiver{state};
    auto sender = bexec::just(1) | bexec::then([](int value) { return value + 1; });
    auto operation = bexec::connect(std::move(sender), receiver);
    static_assert(bexec::operation_state<decltype(operation)>);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 2);
}

void test_just_supports_move_only_values() {
    auto state = std::make_shared<shared_state>();
    auto operation =
        bexec::connect(bexec::just(std::make_unique<int>(42)), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 42);
}

void test_just_error_and_stopped() {
    {
        auto state = std::make_shared<shared_state>();
        auto operation = bexec::connect(bexec::just_error(std::string{"failed"}),
                                        any_receiver{state});

        bexec::start(operation);
        CHECK(state->signal == signal_kind::error);
    }

    {
        auto state = std::make_shared<shared_state>();
        auto operation = bexec::connect(bexec::just_stopped(), any_receiver{state});

        bexec::start(operation);
        CHECK(state->signal == signal_kind::stopped);
    }
}

void test_then_void_and_exception() {
    {
        auto state = std::make_shared<shared_state>();
        bool called = false;
        auto sender = bexec::just(5) | bexec::then([&](int) { called = true; });
        auto operation = bexec::connect(std::move(sender), any_receiver{state});

        bexec::start(operation);
        CHECK(called);
        CHECK(state->signal == signal_kind::value);
    }

    {
        auto state = std::make_shared<shared_state>();
        auto sender = bexec::just() | bexec::then([] {
            throw std::runtime_error("boom");
            return 1;
        });
        auto operation = bexec::connect(std::move(sender), any_receiver{state});

        bexec::start(operation);
        CHECK(state->signal == signal_kind::error);
        CHECK(static_cast<bool>(state->exception));
    }
}

void test_stop_token_callbacks() {
    bexec::inplace_stop_source source;
    auto token = source.get_token();

    int callbacks = 0;
    {
        bexec::inplace_stop_callback callback{token, [&] { ++callbacks; }};
        CHECK(!token.stop_requested());
        CHECK(source.request_stop());
        CHECK(callbacks == 1);
    }

    bexec::inplace_stop_callback immediate{token, [&] { ++callbacks; }};
    CHECK(callbacks == 2);
    CHECK(token.stop_requested());
}

void test_env_query_and_schedule_stop_awareness() {
    bexec::inplace_stop_source source;
    source.request_stop();

    using env_type = bexec::env_with_stop_token<>;
    env_type env{source.get_token()};

    bexec::io_context context;
    auto state = std::make_shared<shared_state>();
    env_receiver<env_type> receiver{state, env};

    auto operation = bexec::connect(bexec::schedule(context.get_scheduler()), receiver);
    bexec::start(operation);

    CHECK(state->signal == signal_kind::stopped);
    CHECK(context.run() == 0);
}

void test_scheduler_sender() {
    bexec::io_context context;
    bool ran = false;
    auto state = std::make_shared<shared_state>();

    auto sender = bexec::schedule(context.get_scheduler()) | bexec::then([&] { ran = true; });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(!ran);
    CHECK(state->signal == signal_kind::none);
    CHECK(context.run() == 1);
    CHECK(ran);
    CHECK(state->signal == signal_kind::value);
}

void test_repeat_until_sync_and_many_iterations() {
    {
        int count = 0;
        auto sender = bexec::repeat_until(
            [&] {
                return bexec::just() | bexec::then([&] { ++count; });
            },
            [&] { return count == 3; });

        auto state = std::make_shared<shared_state>();
        auto operation = bexec::connect(std::move(sender), any_receiver{state});
        bexec::start(operation);

        CHECK(count == 3);
        CHECK(state->signal == signal_kind::value);
    }

    {
        int count = 0;
        auto sender = bexec::repeat_until(
            [&] {
                return bexec::just() | bexec::then([&] { ++count; });
            },
            [&] { return count == 10000; });

        auto state = std::make_shared<shared_state>();
        auto operation = bexec::connect(std::move(sender), any_receiver{state});
        bexec::start(operation);

        CHECK(count == 10000);
        CHECK(state->signal == signal_kind::value);
    }
}

void test_repeat_until_async_error_and_stopped() {
    {
        bexec::io_context context;
        int count = 0;
        auto sender = bexec::repeat_until(
            [&] {
                return bexec::schedule(context.get_scheduler()) |
                       bexec::then([&] { ++count; });
            },
            [&] { return count == 5; });

        auto state = std::make_shared<shared_state>();
        auto operation = bexec::connect(std::move(sender), any_receiver{state});
        bexec::start(operation);

        CHECK(context.run() == 5);
        CHECK(count == 5);
        CHECK(state->signal == signal_kind::value);
    }

    {
        auto sender = bexec::repeat_until([] { return bexec::just_error(std::string{"bad"}); },
                                          [] { return false; });

        auto state = std::make_shared<shared_state>();
        auto operation = bexec::connect(std::move(sender), any_receiver{state});
        bexec::start(operation);

        CHECK(state->signal == signal_kind::error);
    }

    {
        auto sender = bexec::repeat_until([] { return bexec::just_stopped(); },
                                          [] { return false; });

        auto state = std::make_shared<shared_state>();
        auto operation = bexec::connect(std::move(sender), any_receiver{state});
        bexec::start(operation);

        CHECK(state->signal == signal_kind::stopped);
    }
}

void test_when_all_success_error_and_stopped() {
    {
        auto state = std::make_shared<shared_state>();
        auto operation = bexec::connect(bexec::when_all(bexec::just(), bexec::just()),
                                        any_receiver{state});

        bexec::start(operation);
        CHECK(state->signal == signal_kind::value);
    }

    {
        using sender_type = decltype(bexec::when_all(bexec::just(), bexec::just_error(7)));
        using variant_type = sender_type::error_variant;
        static_assert(std::variant_size_v<variant_type> >= 2);

        variant_receiver<variant_type> receiver;
        auto state = receiver.state;
        auto error = receiver.error;
        auto operation = bexec::connect(bexec::when_all(bexec::just(), bexec::just_error(7)),
                                        receiver);

        bexec::start(operation);
        CHECK(state->signal == signal_kind::error);
        CHECK(error->has_value());
        CHECK(std::holds_alternative<int>(**error));
        CHECK(std::get<int>(**error) == 7);
    }

    {
        using sender_type = decltype(bexec::when_all(bexec::just_error(3),
                                                     bexec::just_error(std::string{"later"})));
        using variant_type = sender_type::error_variant;
        static_assert(std::variant_size_v<variant_type> >= 3);

        variant_receiver<variant_type> receiver;
        auto state = receiver.state;
        auto error = receiver.error;
        auto operation = bexec::connect(bexec::when_all(bexec::just_error(3),
                                                        bexec::just_error(std::string{"later"})),
                                        receiver);

        bexec::start(operation);
        CHECK(state->signal == signal_kind::error);
        CHECK(error->has_value());
        CHECK(std::holds_alternative<int>(**error));
        CHECK(std::get<int>(**error) == 3);
    }

    {
        auto state = std::make_shared<shared_state>();
        auto operation = bexec::connect(bexec::when_all(bexec::just(), bexec::just_stopped()),
                                        any_receiver{state});

        bexec::start(operation);
        CHECK(state->signal == signal_kind::stopped);
    }
}

void test_when_all_scheduler_children() {
    bexec::io_context context;
    int count = 0;

    auto first = bexec::schedule(context.get_scheduler()) | bexec::then([&] { ++count; });
    auto second = bexec::schedule(context.get_scheduler()) | bexec::then([&] { ++count; });
    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(bexec::when_all(std::move(first), std::move(second)),
                                    any_receiver{state});

    bexec::start(operation);
    CHECK(context.run() == 2);
    CHECK(count == 2);
    CHECK(state->signal == signal_kind::value);
}

bexec::task<int> scheduled_value(bexec::io_context::scheduler scheduler) {
    co_await scheduler.schedule_awaitable();
    co_return 42;
}

bexec::task<void> scheduled_void(bexec::io_context::scheduler scheduler, bool& ran) {
    co_await scheduler.schedule_awaitable();
    ran = true;
}

void test_coroutine_task() {
    bexec::io_context context;
    auto value_task = scheduled_value(context.get_scheduler());

    value_task.start();
    CHECK(!value_task.done());
    CHECK(context.run() == 1);
    CHECK(value_task.done());
    CHECK(value_task.result() == 42);

    bool ran = false;
    auto void_task = scheduled_void(context.get_scheduler(), ran);
    void_task.start();
    CHECK(context.run() == 1);
    CHECK(void_task.done());
    void_task.result();
    CHECK(ran);
}

} // namespace

int main() {
    test_concepts_and_just_then();
    test_just_supports_move_only_values();
    test_just_error_and_stopped();
    test_then_void_and_exception();
    test_stop_token_callbacks();
    test_env_query_and_schedule_stop_awareness();
    test_scheduler_sender();
    test_repeat_until_sync_and_many_iterations();
    test_repeat_until_async_error_and_stopped();
    test_when_all_success_error_and_stopped();
    test_when_all_scheduler_children();
    test_coroutine_task();

    if (failures != 0) {
        std::cerr << failures << " test failure(s)\n";
        return 1;
    }

    std::cout << "all tests passed\n";
    return 0;
}
