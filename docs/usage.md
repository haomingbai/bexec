# Usage

## Basic Sender/Receiver

A sender is connected to a receiver to produce an operation state. Starting the
operation eventually delivers exactly one terminal signal to the receiver:

- `set_value(args...)`
- `set_error(error)`
- `set_stopped()`

```cpp
struct receiver {
    void set_value(int value) {
        // use value
    }

    void set_error(std::exception_ptr error) {
        // handle error
    }

    void set_stopped() {
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

`then(fn)` transforms `set_value`. It can be used directly or as a pipeable
adaptor.

```cpp
auto s = bexec::just(1) | bexec::then([](int x) {
    return x + 1;
});
```

If `fn` returns `void`, the downstream receiver gets `set_value()` with no
values. If `fn` returns a value, the downstream receiver gets that value.

When exceptions are enabled, exceptions thrown by `fn` are delivered as
`set_error(std::exception_ptr)`. With exceptions disabled, throwing callables
are not supported.

## Scheduler

`io_context` is a small FIFO event loop. Its scheduler produces senders through
`schedule(scheduler)`.

```cpp
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
auto env = bexec::get_env(receiver);
auto token = bexec::query(env, bexec::get_stop_token);
```

Receivers can expose `get_env()`. If they do not, `get_env(receiver)` returns
`empty_env`, whose stop token never requests stop.

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
bexec::io_context context;
auto sched = context.get_scheduler();

auto a = bexec::schedule(sched) | bexec::then([] {});
auto b = bexec::schedule(sched) | bexec::then([] {});

auto op = bexec::connect(bexec::when_all(std::move(a), std::move(b)), receiver{});
bexec::start(op);
context.run();
```

All-success completion sends `set_value()` with no values. On the first error
or stopped signal, `when_all` requests stop through its internal stop source and
waits for all started children to finish before completing the receiver. Errors
are delivered as a `std::variant` of the child error types plus
`std::exception_ptr`.

## Coroutines

`task<T>` is a small lazy coroutine task used with the internal scheduler.

```cpp
bexec::task<int> run_on_scheduler(bexec::io_context::scheduler sched) {
    co_await sched.schedule_awaitable();
    co_return 42;
}

bexec::io_context context;
auto task = run_on_scheduler(context.get_scheduler());

task.start();
context.run();
int value = task.result();
```

`task<T>` is intentionally small. It is not a sender, not a general coroutine
framework, and does not provide cancellation propagation beyond what the awaited
scheduler operation supports.
