# bexec

bexec is a small C++20 sender/receiver concurrency library inspired by P2300.
It is intended as a temporary, practical bridge while standardized C++26
execution support becomes widely available in mainstream stable toolchains.

This is not a full P2300 implementation. The current code is an MVP with a
member-customization API, a small scheduler, basic stop tokens, simple
environment queries, a few sender factories/adaptors, and focused tests.

## What It Is

- A header-only C++20 library under `include/bexec/bexec.hpp`.
- A minimal sender/receiver vocabulary:
  - `start`
  - `connect`
  - `schedule`
  - `set_value`
  - `set_error`
  - `set_stopped`
- Member-based customization. For example, `connect(sender, receiver)` calls
  `sender.connect(receiver)`.
- Basic sender factories and adaptors:
  - `just`
  - `just_error`
  - `just_stopped`
  - `then`
  - pipe syntax: `sender | then(fn)`
- A simple `io_context` scheduler.
- Minimal `inplace_stop_source`, `inplace_stop_token`, and
  `inplace_stop_callback`.
- Minimal environment/query support with `get_env`, `query`,
  `get_stop_token`, and `get_scheduler` tags.
- `repeat_until` for sequential repetition using a sender factory.
- `when_all` for structured startup and first-terminal aggregation.
- A small coroutine `task<T>` and scheduler awaitable.

## What It Is Not

- It is not a complete implementation of P2300.
- It does not use `tag_invoke`.
- It does not depend on `stdexec`.
- It does not implement the full P2300 completion-signatures model.
- It does not aggregate successful `when_all` values. Successful children are
  currently value-discarding and final completion is `set_value()`.
- It does not provide allocator customization, domains, bulk execution,
  full async scopes, advanced scheduler properties, or ABI-stable boundaries.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The project builds tests and examples by default. Disable them with:

```sh
cmake -S . -B build -DBEXEC_BUILD_TESTS=OFF -DBEXEC_BUILD_EXAMPLES=OFF
```

## Quick Examples

```cpp
#include <bexec/bexec.hpp>

struct receiver {
    void set_value(int) {}
    void set_error(std::exception_ptr) {}
    void set_stopped() {}
};

auto s = bexec::just(1) | bexec::then([](int x) { return x + 1; });
auto op = bexec::connect(std::move(s), receiver{});
bexec::start(op);
```

```cpp
bexec::io_context context;
auto sched = context.get_scheduler();

auto s = bexec::schedule(sched) | bexec::then([] {
    // Runs when context.run() executes queued work.
});

auto op = bexec::connect(std::move(s), receiver{});
bexec::start(op);
context.run();
```

## Current Limitations

- Receiver customization is member-only: `r.set_value(...)`,
  `r.set_error(error)`, and `r.set_stopped()`.
- Sender customization is member-only: `sender.connect(receiver)`.
- Scheduler customization is member-only: `scheduler.schedule()`.
- `repeat_until(factory, predicate)` uses a sender-producing factory. Reusing
  the same operation state across iterations is intentionally not supported.
- `repeat_until` discards child values and completes with `set_value()` when
  the predicate returns true.
- `when_all` discards child success values. It stores the first terminal error
  in a `std::variant` of declared child error types plus
  `std::exception_ptr`.
- If exceptions are disabled, `then` cannot translate thrown exceptions to
  `std::exception_ptr`; throwing code is outside the supported configuration.

See `docs/usage.md`, `docs/design.md`, and `docs/maintenance.md` for details.
