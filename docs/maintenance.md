# Maintenance

## Code Layout

- `include/bexec/bexec.hpp`: public umbrella header.
- `include/bexec/*.hpp`: public feature headers.
- `include/bexec/detail/*.hpp`: implementation helpers that are not part of
  the public API contract.
- `tests/test_main.cpp`: CTest executable entry point.
- `tests/test_support.hpp`: shared test receiver and assertion helpers.
- `tests/test_*.cpp`: one feature module per test file.
- `examples/basic.cpp`: compiled examples covering the main features.
- `docs/`: usage, design, maintenance, and roadmap documentation.
- `PROGRESS.md`: checkpoint log with commands run and known limitations.

The library is currently header-only. Add `src/` only when there is a concrete
need for separately compiled implementation. Keep implementation-only helper
types under `include/bexec/detail/`.

## Adding A Sender

New senders should:

1. Own the data needed to create an operation state.
2. Expose `using completion_signatures = ...`.
3. Provide `connect(receiver)` overloads.
4. Return an operation state with a `start()` member.
5. Deliver exactly one terminal receiver signal.

Prefer rvalue `connect` for move-only state and const lvalue `connect` only
when the sender is safely copyable.

## Adding An Adaptor

Adaptor senders should wrap an upstream sender and connect it to an internal
receiver. The internal receiver should:

- forward `set_error`,
- forward `set_stopped`,
- preserve or deliberately override `get_env`,
- document any value transformation or value discarding.

Pipeable adaptors should follow the small `then_closure` pattern rather than
trying to implement the full P2300 adaptor-closure model.

## Adding A Scheduler

A scheduler type should provide:

```cpp
auto schedule() const;
```

The returned sender should complete on the scheduler's execution context.
Document whether scheduling is FIFO, thread-safe, blocking, inline-capable, or
best-effort.

If the scheduler can observe cancellation, check the receiver environment's
`get_stop_token` before delivering `set_value()`.

## Testing Expectations

Every new feature should include tests for:

- success completion,
- error completion where applicable,
- stopped/cancellation behavior where applicable,
- move-only values if state ownership is involved,
- synchronous completion if the feature can be used with `just`,
- scheduler-based asynchronous completion when practical.

For adaptors and algorithms, add at least one test that composes with another
sender/adaptor.

## Thread-Safety Expectations

Only documented types are thread-safe:

- `io_context::post`, `io_context::enqueue`, `io_context::stop`, and
  `io_context::stopped` are mutex-protected.
- `inplace_stop_source` and callback registration are thread-safe for the
  documented stop-token use cases.
- Operation states are not generally thread-safe unless a specific operation
  documents stronger guarantees.

Receivers should assume one terminal signal. They should not assume signals are
delivered on a specific thread unless the scheduler or sender documents that
behavior.

## Documentation Expectations

Public API additions need Doxygen comments in the header and matching prose in
`docs/usage.md` or `docs/design.md`. If a feature intentionally diverges from
P2300, document that explicitly.

## Release Hygiene

Before considering a change complete:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Update `PROGRESS.md` with the checkpoint, files changed, tests run, and known
limitations.
