# bexec

bexec is a small C++20 sender/receiver concurrency library inspired by P2300.
It is intended as a temporary, practical bridge while standardized C++26
execution support becomes widely available in mainstream stable toolchains.

This is not a full P2300 implementation. The current code is a C++26-aligned
subset with a member-customization API, environment-aware P2300-style
completion signatures, small schedulers, in-place stop tokens, simple
environment queries, sender factories/adaptors, and focused tests.

## What It Is

- A header-only C++20 library with an umbrella header at
  `include/bexec/bexec.hpp` and feature headers under `include/bexec/`.
- Role-oriented public headers for operation states, receivers, senders,
  schedulers, queries, and completion signatures.
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
  - `upon_error`
  - `upon_stopped`
  - `let_value`
  - `let_error`
  - `let_stopped`
  - `into_variant`
  - pipe syntax: `sender | then(fn)`
- A simple `io_context` scheduler. Despite the name, it does not implement
  file, socket, or OS IO; it is a FIFO execution context using the familiar
  run-loop pattern.
- A stack-owned `run_loop` scheduler for `this_thread::sync_wait`, tests, and
  small local scheduling scenarios.
- Minimal `inplace_stop_source`, `inplace_stop_token`, and
  `inplace_stop_callback`.
- Minimal environment/query support with `get_env`, `query`,
  `get_stop_token`, `get_allocator`, `get_scheduler`, and
  `get_delegation_scheduler` tags.
- `repeat_until` for sequential repetition using a sender factory.
- `when_all` for structured startup, value aggregation, raw error delivery,
  first-terminal stop propagation, and downstream cancellation propagation.
- `when_all_with_variant` for child senders with multiple value alternatives.
- `starts_on(scheduler, sender)` and `on(scheduler, sender)`.
- Standard-style `simple_counting_scope`, `counting_scope`, detached `spawn`,
  and `spawn_future` for scope-tracked eager work.
- `bexec::this_thread::sync_wait(sender)` and
  `bexec::this_thread::sync_wait_with_variant(sender)`.
- A small coroutine `task<T>` helper.

## What It Is Not

- It is not a complete implementation of P2300.
- It does not use `tag_invoke`.
- It does not depend on `stdexec`.
- It does not provide domains, bulk execution, public `associate`,
  `let_async_scope`, advanced scheduler properties, or ABI-stable boundaries.
- It does not provide the nonstandard `bexec::sync_wait` alias or
  `on(sender, scheduler)` overload.

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

## Formatting

```sh
scripts/format.sh
```

The formatter auto-detects existing source directories and applies Google C++
style with `clang-format`.

Naming intentionally follows the C++ standard library rather than Google C++
names: use `snake_case` throughout, with trailing underscores for private data
members.

## Quick Examples

```cpp
#include <bexec/bexec.hpp>

struct receiver {
    void set_value(int) noexcept {}
    void set_error(std::exception_ptr) noexcept {}
    void set_stopped() noexcept {}
};

auto s = bexec::just(1) | bexec::then([](int x) { return x + 1; });
auto op = bexec::connect(std::move(s), receiver{});
bexec::start(op);
```

```cpp
auto result = bexec::this_thread::sync_wait(
    bexec::when_all(bexec::just(1), bexec::just(std::string{"ok"})));

if (result) {
    auto& [number, text] = *result;
}
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

- Receiver customization is member-only and terminal receiver members must be
  `noexcept`: `r.set_value(...)`, `r.set_error(error)`, and
  `r.set_stopped()`.
- Sender customization is member-only: `sender.connect(receiver)`.
- Scheduler customization is member-only: `scheduler.schedule()`.
- `repeat_until(factory, predicate)` uses a sender-producing factory. Reusing
  the same operation state across iterations is intentionally not supported.
- `repeat_until` discards child values and completes with `set_value()` when
  the predicate returns true.
- `when_all()` and `when_all_with_variant()` require at least one child sender.
- Plain `when_all` requires each child to have at most one value completion
  alternative. Use `when_all_with_variant` when a child has multiple possible
  value shapes.
- `spawn(sender, token, env)` accepts detached senders that complete only with
  `set_value()` and/or `set_stopped()`. `spawn_future(sender, token, env)`
  eagerly starts the input sender and returns a move-only sender that consumes
  the stored result. Destroying that returned sender before the child completes
  requests stop for the child. Join receivers must expose `get_scheduler`
  through their environment.
- Demo-only `io_context::post(std::function<void()>)` may allocate. The
  library operation states added for scheduling and waiting use in-place
  operation storage.
- If exceptions are disabled, `then` cannot translate thrown exceptions to
  `std::exception_ptr`; the same limitation applies to other adaptors that
  report callable/connect failures as `std::exception_ptr`.

See `docs/usage.md`, `docs/design.md`, and `docs/maintenance.md` for details.
