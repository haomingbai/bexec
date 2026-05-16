# Design

## Goals

bexec provides a compact C++20 sender/receiver library with production-quality
structure and tests, while avoiding the implementation size and instability of
a full P2300 implementation.

The design favors readable member customization and explicit limitations over
heavy metaprogramming.

## Concepts

The public concepts are intentionally small:

- `operation_state`: `start(op)` is valid.
- `sender`: the type is move-constructible and exposes P2300-style
  `completion_signatures` metadata.
- `sender_to`: `connect(sender, receiver)` returns an operation state.
- `scheduler`: `schedule(scheduler)` returns a sender.
- `stop_token`: supports `stop_requested()` and callback registration.
- `stop_source`: supports `request_stop()`, `stop_requested()`, and
  `get_token()`.

The concepts are compatibility checks for this library, not complete P2300
semantic contracts.

## CPOs And Member Customization

bexec uses CPO-style function objects, but the customization path is explicitly
member-based:

- `start(op)` calls `op.start()` on a non-rvalue operation state. The member
  must be `noexcept` and return `void`.
- `connect(sender, receiver)` calls `sender.connect(receiver)`.
- `schedule(scheduler)` calls `scheduler.schedule()`.
- `set_value(receiver, args...)` calls `receiver.set_value(args...)` on a
  non-const rvalue receiver. The member must be `noexcept` and return `void`.
- `set_error(receiver, error)` and `set_stopped(receiver)` follow the same
  terminal-receiver rule.
- `get_env(receiver)` calls a const `receiver.get_env()` when available.
- `query(env, tag)` invokes the query object, so `query(env, get_stop_token)`
  and `get_stop_token(env)` use the same path. `get_allocator` follows the
  same query path and falls back to `std::allocator<std::byte>`.

There is no `tag_invoke` support. This is intentional. It keeps overload
resolution easy to reason about, keeps diagnostics smaller, and avoids exposing
unstable implementation hooks while the library is still an MVP.

## Completion Metadata

`completion_signatures<Sigs...>` uses P2300-style function types:

```cpp
using completions = bexec::completion_signatures<
    bexec::set_value_t(int),
    bexec::set_error_t(std::exception_ptr),
    bexec::set_stopped_t()>;
```

The public helpers `completion_signatures_of_t`, `value_types_of_t`,
`error_types_of_t`, and `sends_stopped` inspect this signature pack. `then`
transforms `set_value_t(Args...)` alternatives for simple invocable cases and
adds `std::exception_ptr` to possible errors. `let_value`, `let_error`, and
`let_stopped` replace the selected upstream completion signatures with the
completion signatures of the sender returned by the user callable, preserve
non-selected completions, and add `std::exception_ptr` for callable/connect
exceptions. `when_all` declares the actual error it sends: a `std::variant`
containing child error alternatives plus `std::exception_ptr`.

## Ownership Model

Senders are lightweight values. `connect` consumes or copies a sender into an
operation state, depending on value category and copyability.

Receivers are moved into operation states. A receiver must remain valid until it
receives one terminal signal. The library does not retain references to a
receiver after terminal completion except where a user-provided receiver type
itself stores shared state.

Operation states are single-start objects. Starting an operation more than once
is outside the supported contract.

Operation states are not required to be copyable or movable. Implementations
that contain queued callbacks or child receivers pointing into their own state
delete copy and move operations.

For asynchronous senders, the operation state is expected to remain alive until
the terminal signal is delivered. Internal queued callbacks may therefore keep
pointers into their operation state; they must not move receivers into detached
shared ownership to extend lifetime beyond the operation.

## Environment Model

The environment model is deliberately simple. A receiver may provide:

```cpp
auto get_env() const;
```

The returned environment answers queries through member functions:

```cpp
auto query(bexec::get_stop_token_t) const noexcept;
auto query(bexec::get_allocator_t) const noexcept;
```

If a receiver has no `get_env()`, `empty_env` is used. `empty_env` answers
`get_stop_token` with `never_stop_token` and `get_allocator` with
`std::allocator<std::byte>`.

`env_with_stop_token<BaseEnv>` overrides `get_stop_token` and delegates other
queries to the wrapped environment. `when_all` uses this to give children a
shared cancellation token.

## Stop Token Model

`inplace_stop_source`, `inplace_stop_token`, and `inplace_stop_callback` provide
a small callback-based stop mechanism.

Threading guarantees:

- `request_stop()` is thread-safe.
- `stop_requested()` is thread-safe.
- Callback registration is thread-safe relative to `request_stop()`.
- If stop was already requested, registration invokes the callback immediately.
- Callback invocation is one-shot.
- Destroying a callback registration prevents future invocation if the callback
  has not already been selected for invocation.

Callbacks are expected not to throw. If a callback throws, the implementation
terminates.

## io_context Model

`io_context` is a mutex-protected FIFO execution context. The name follows a
common scheduler/run-loop pattern; this type does not provide file, socket, or
OS IO operations.

`io_context` owns a FIFO queue of `std::function<void()>`.
`post` and `enqueue` are thread-safe and return `false` if the context is
stopped.

`run_one()` executes one queued item if available. `run()` repeatedly calls
`run_one()` until the queue is empty or the context is stopped. This is simpler
than `asio::io_context`: there is no work guard and `run()` does not block
waiting for future work after the queue becomes empty.

`schedule(scheduler)` returns a sender that posts receiver completion to the
context. If the receiver's stop token is already requested, it completes with
`set_stopped()` without posting. The posted handler checks the stop token again
before delivering `set_value()`. The posted handler captures a pointer to the
operation state; the receiver remains stored in that operation state until
completion.

## let_* Operation State

`let_value(sender, fn)`, `let_error(sender, fn)`, and
`let_stopped(sender, fn)` are continuation adaptors. They replace only one
upstream completion kind:

- `let_value` calls `fn(args...)` when the upstream sends `set_value(args...)`.
- `let_error` calls `fn(error)` when the upstream sends `set_error(error)`.
- `let_stopped` calls `fn()` when the upstream sends `set_stopped()`.

The callable must return a sender. That child sender is connected to an
internal child receiver and started immediately. The final downstream receiver
gets the child sender's terminal signal. Non-selected upstream completions are
forwarded directly to the downstream receiver without invoking the callable.

The operation state stores:

- the user callable,
- the downstream receiver,
- the upstream operation in `manual_lifetime`,
- storage for one selected child operation.

The child operation storage is a small in-place type switch over the operation
types implied by the selected upstream completion signatures. It does not use
heap allocation, `std::function`, or `std::optional`, and it does not require
child operation states to be movable.

Receivers used by the upstream and child operations store only a pointer back
to the parent operation state. For that reason, the `let_*` operation state
deletes copy and move operations. This follows the library-wide operation
lifetime rule: the operation state must remain alive until the terminal signal
is delivered.

When exceptions are enabled, exceptions thrown while invoking the callable or
connecting the child sender are delivered as `set_error(std::exception_ptr)`.
With exceptions disabled, throwing callables are not supported.

## repeat_until State Machine

`repeat_until(factory, predicate)` uses a sender-producing factory. Each
iteration creates a fresh sender and connects it to an internal receiver.

This avoids restarting the same operation state, which would be invalid for
many senders.

The implementation handles synchronous and asynchronous completion through a
small trampoline:

- Before starting a child operation, it marks the child as pending.
- If the child completes synchronously inside `start()`, the child receiver
  clears the pending flag and records whether another iteration is needed.
- The outer loop continues directly, so it does not recursively call `start()`.
- If the child remains pending after `start()` returns, the loop exits.
- A later asynchronous completion callback re-enters the drain loop if another
  iteration is needed.

Child values are discarded. Errors and stopped signals are propagated.
Cancellation is checked through the receiver environment before each iteration.

## when_all Operation State

`when_all` stores operation-owned state containing:

- the final receiver,
- a remaining-child count,
- a mutex,
- an internal `inplace_stop_source`,
- the first terminal error/stopped state,
- an optional error `std::variant`.

The state is a direct member of the operation state. Child receivers store a
pointer to it; they do not share-own it. This relies on the P2300 operation
lifetime rule that the operation state remains alive until completion.

All child operations are started. On the first error or stopped signal,
`when_all` records that terminal state and requests stop through the shared stop
source. Children receive that token through their environment.

The final receiver is completed only after all started children have finished.
If the first terminal state was an error, the receiver gets
`set_error(error_variant)`. If it was stopped, the receiver gets
`set_stopped()`. If all children succeed, the receiver gets `set_value()`.

Current MVP simplification: successful child values are discarded.

## Coroutine Design

`task<T>` is a lazy coroutine task. It starts when `start()` is called and
stores either a result or an exception.

The coroutine support is intentionally scoped to a minimal task helper.
Schedulers expose scheduling through `schedule(scheduler)`, not through
awaiter member functions. Future sender-to-awaitable support should be built
with an internal receiver that connects to a sender and resumes the coroutine
from receiver completion signals.
