# Maintenance

## Code Layout

- `include/bexec/bexec.hpp`: public umbrella header.
- `include/bexec/completion_signatures.hpp`: P2300-style completion signature
  pack and helper type-list utilities.
- `include/bexec/operation_state.hpp`, `receiver.hpp`, `sender.hpp`,
  `scheduler.hpp`, and `query.hpp`: role-oriented public vocabulary headers.
- `include/bexec/cpo.hpp`: aggregate include for CPO entities only; keep
  feature implementations in their role or feature headers.
- `include/bexec/io_context/`: the FIFO execution context and its related
  detail helpers.
- `include/bexec/*.hpp`: other public feature headers.
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

## Naming Style

The project follows standard-library-style naming because bexec intentionally
resembles C++ vocabulary and execution facilities. Use `snake_case` for public
APIs, concepts, types, functions, variables, namespaces, and file names rather
than Google-style `PascalCase` type names.

Private data members use a trailing underscore, for example `receiver_` and
`operation_`. Avoid leading underscores and names reserved to the implementation.

## Header Protection

All headers use a Doxygen file header, `#pragma once`, and a conventional
include guard. Keep the file header first so copyright and file-purpose
metadata are visible at the top, then place `#pragma once` and the guard
immediately after it:

```cpp
/**
 * @file include/bexec/example.hpp
 * @brief Example header summary.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Briefly describes the primary purpose of this header.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_EXAMPLE_HPP_
#define BEXEC_INCLUDE_BEXEC_EXAMPLE_HPP_

// Header contents.

#endif  // BEXEC_INCLUDE_BEXEC_EXAMPLE_HPP_
```

Guard names are derived from the repository-relative path, uppercased with
non-alphanumeric characters converted to underscores, and wrapped with the
`BEXEC_` prefix and trailing underscore.

## Formatting

Run `scripts/format.sh` before submitting broad source changes. The script
auto-detects existing source directories such as `include/`, `src/`, `tests/`,
`examples/`, and `benchmarks/`, then formats C and C++ files with
`clang-format --style=Google`. This controls whitespace and layout only; naming
still follows the standard-library-style `snake_case` rules above.

## Adding A Sender

New senders should:

1. Own the data needed to create an operation state.
2. Expose P2300-style `using completion_signatures =
   bexec::completion_signatures<set_value_t(...), set_error_t(...), ...>`.
3. Provide `connect(receiver)` overloads.
4. Return an operation state with a `noexcept void start()` member.
5. Deliver exactly one terminal receiver signal.

Prefer rvalue `connect` for move-only state and const lvalue `connect` only
when the sender is safely copyable.

Asynchronous senders may capture pointers into their operation state because
the operation state is required to remain alive until completion. Do not move a
receiver into detached `shared_ptr` storage merely to keep it alive for a
callback. If a sender or adaptor really needs heap storage inside the operation
state, allocate it through `query(get_env(receiver), get_allocator)` and allow
the query object to fall back to `std::allocator<std::byte>`.

Delete copy and move operations on operation states when callbacks, child
receivers, or other stored objects keep pointers or references into the
operation state.

Use `include/bexec/detail/manual_lifetime.hpp` when an operation state needs
optional storage for child operation states. Unlike `std::optional`,
`detail::manual_lifetime<T>` can construct a non-movable operation directly
from a factory result, which avoids reintroducing hidden move-construction
requirements.

## Adding An Adaptor

Adaptor senders should wrap an upstream sender and connect it to an internal
receiver. The internal receiver should:

- forward `set_error`,
- forward `set_stopped`,
- preserve or deliberately override `get_env`,
- document any value transformation or value discarding.

Adaptor operation states should construct child operation states directly and
must not require child operations to be move-constructible.

Pipeable adaptors should follow the small `then_closure` pattern rather than
trying to implement the full P2300 adaptor-closure model.

Public callable factories and adaptors should be named function-object
instances, not standalone free functions. Define a small `*_t` struct with
`operator()` overloads and expose it as an `inline constexpr` object, following
`just_t`, `then_t`, `repeat_until_t`, and `when_all_t`.

## Adding A Scheduler

A scheduler type should provide:

```cpp
auto schedule() const;
```

The returned sender should complete on the scheduler's execution context.
Document whether scheduling is FIFO, thread-safe, blocking, inline-capable, or
best-effort.

Do not use the `io_context` directory as a place for general IO utilities. In
this project it names the run-loop pattern only.

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
