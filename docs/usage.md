# Usage

## Basic Sender/Receiver

A sender is connected to a receiver to produce an operation state. Starting the
operation eventually delivers exactly one terminal signal to the receiver:

- `set_value(args...)`
- `set_error(error)`
- `set_stopped()`

```cpp
struct receiver {
    void set_value(int value) noexcept {
        // use value
    }

    void set_error(std::exception_ptr error) noexcept {
        // handle error
    }

    void set_stopped() noexcept {
        // handle cancellation
    }
};

auto op = bexec::connect(bexec::just(1), receiver{});
bexec::start(op);
```

Receivers are moved into operation states. The library assumes a receiver stays
valid until it receives its terminal signal.

## just

`just(values...)` completes synchronously with `set_value(values...)`.
Move-only values are supported when the sender is connected as an rvalue.

```cpp
auto op = bexec::connect(
    bexec::just(std::make_unique<int>(42)),
    receiver{});
bexec::start(op);
```

## just_error and just_stopped

```cpp
auto error_op = bexec::connect(bexec::just_error(std::string{"failed"}), receiver{});
auto stopped_op = bexec::connect(bexec::just_stopped(), receiver{});
```

`just_error(error)` sends `set_error(error)`. `just_stopped()` sends
`set_stopped()`.

## then

`then(fn)` transforms `set_value`. `upon_error(fn)` transforms `set_error`.
`upon_stopped(fn)` transforms `set_stopped`. They can be used directly or as
pipeable adaptors.

```cpp
auto s = bexec::just(1) | bexec::then([](int x) {
    return x + 1;
});

auto recovered = bexec::just_error(5) | bexec::upon_error([](int code) {
    return code + 1;
});

auto stopped = bexec::just_stopped() | bexec::upon_stopped([] {
    return 42;
});
```

If `fn` returns `void`, the downstream receiver gets `set_value()` with no
values. If `fn` returns a value, the downstream receiver gets that value.

When exceptions are enabled, exceptions thrown by `fn` are delivered as
`set_error(std::exception_ptr)`. With exceptions disabled, throwing callables
are not supported.

Non-selected completions are forwarded unchanged.

## Scheduler

`io_context` is a small FIFO execution context. Despite the name, it does not
perform file, socket, or OS IO. Its scheduler produces senders through
`schedule(scheduler)`.

```cpp
#include <bexec/io_context/io_context.hpp>

bexec::io_context context;
auto sched = context.get_scheduler();

auto s = bexec::schedule(sched) | bexec::then([] {
    // Runs on the thread that calls context.run().
});

auto op = bexec::connect(std::move(s), receiver{});
bexec::start(op);
context.run();
```

`post` and `enqueue` are thread-safe. `run()` drains queued work until the queue
is empty or `stop()` is requested. It is not a work-guarded blocking loop.

`run_loop` is a stack-owned FIFO scheduler whose queue stores operation
pointers intrusively. It is useful for local tests, hand-written blocking
waits, and `this_thread::sync_wait`.

```cpp
bexec::run_loop loop;

auto op = bexec::connect(
    bexec::starts_on(loop.get_scheduler(), bexec::just(7)),
    receiver{});

bexec::start(op);
loop.run_one();
```

`starts_on(scheduler, sender)` first completes `schedule(scheduler)` and then
starts the child sender on that execution resource.

`on(scheduler, sender)` starts the child through the target scheduler and
schedules final delivery back through `get_scheduler(get_env(receiver))`.
Connecting an `on` sender is ill-formed when the receiver environment has no
`get_scheduler` query.

```cpp
bexec::run_loop target;
bexec::run_loop caller;

// The receiver environment must answer get_scheduler with caller's scheduler.
auto s = bexec::on(target.get_scheduler(), bexec::just(1));
```

## Stop Tokens

```cpp
bexec::inplace_stop_source source;
auto token = source.get_token();

bexec::inplace_stop_callback cb{token, [] {
    // Called when stop is requested.
}};

source.request_stop();
```

Callbacks fire once. If stop has already been requested, registration invokes
the callback immediately.

## Environment Queries

The query model is member-based:

```cpp
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>

auto env = bexec::get_env(receiver);
auto token = bexec::query(env, bexec::get_stop_token);
auto same_token = bexec::get_stop_token(env);
auto allocator = bexec::query(env, bexec::get_allocator);
```

Receivers can expose `get_env()`. If they do not, `get_env(receiver)` returns
`empty_env`, whose stop token never requests stop.

`get_allocator` is also a query tag. If the environment does not provide an
allocator, it falls back to `std::allocator<std::byte>`. Code that adds
allocator-aware heap storage should obtain its allocator through this query
path.

`get_scheduler` and `get_delegation_scheduler` are also query tags. `on` uses
`get_scheduler` from the downstream receiver environment to restore final
delivery.

## repeat_until

`repeat_until(factory, predicate)` repeatedly creates and starts a fresh child
sender. After each successful child completion, it calls `predicate()`. When the
predicate returns true, the repeat sender completes with `set_value()`.

```cpp
int count = 0;

auto repeated = bexec::repeat_until(
    [&] {
        return bexec::just() | bexec::then([&] { ++count; });
    },
    [&] { return count == 10; });

auto op = bexec::connect(std::move(repeated), receiver{});
bexec::start(op);
```

The child sender's values are discarded. The factory form is intentional: it
avoids restarting an operation state and works for move-only senders. The
implementation uses a trampoline so synchronous children such as `just()` do
not recursively call `start()` and do not grow the stack per iteration.

## when_all

`when_all(a, b, ...)` starts all child senders. It completes after all started
children have completed.

```cpp
#include <bexec/io_context/io_context.hpp>

bexec::io_context context;
auto sched = context.get_scheduler();

auto a = bexec::schedule(sched) | bexec::then([] {});
auto b = bexec::schedule(sched) | bexec::then([] {});

auto op = bexec::connect(bexec::when_all(std::move(a), std::move(b)), receiver{});
bexec::start(op);
context.run();
```

All-success completion sends the concatenated child values in argument order:

```cpp
auto result = bexec::this_thread::sync_wait(
    bexec::when_all(bexec::just(1, 2), bexec::just(std::string{"ok"})));

// result has type std::optional<std::tuple<int, int, std::string>>
```

On the first error or stopped signal, `when_all` requests stop through its
internal stop source and waits for all started children to finish before
completing the receiver. Errors are delivered as their original error type;
`std::exception_ptr` is also listed for internal connect/start failures.

Plain `when_all` requires each child sender to have at most one value
completion alternative. Use `when_all_with_variant` for senders with multiple
value alternatives:

```cpp
auto s = bexec::when_all_with_variant(maybe_int_or_string(), bexec::just(3));
```

`when_all()` and `when_all_with_variant()` with zero senders are ill-formed.

## into_variant

`into_variant(sender)` maps every successful value completion into one
`std::variant<std::tuple<...>, ...>` value. Duplicate tuple alternatives are
removed from the variant type. Errors and stopped completion are forwarded.

```cpp
auto s = bexec::into_variant(maybe_int_or_string());
```

## sync_wait

`bexec::this_thread::sync_wait(sender)` starts a sender and blocks the current
thread by running a local `run_loop`. A value completion returns
`std::optional<std::tuple<...>>`; stopped returns `std::nullopt`; errors are
thrown, with `std::exception_ptr` rethrown.

```cpp
auto value = bexec::this_thread::sync_wait(bexec::just(1));
```

`sync_wait_with_variant(sender)` is the variant-returning form for senders with
multiple value alternatives. It returns
`std::optional<std::variant<std::tuple<...>, ...>>`.

## Coroutines

`task<T>` is a small lazy coroutine task helper.

```cpp
#include <bexec/task.hpp>

bexec::task<int> compute_value() {
    co_return 42;
}

auto task = compute_value();

task.start();
int value = task.result();
```

`task<T>` is intentionally small. It is not a sender, not a general coroutine
framework, and does not make scheduler senders directly awaitable. Future
receiver-based coroutine integration is tracked in the roadmap.
