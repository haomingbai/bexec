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
- `sender`: the type is move-constructible and exposes MVP
  `completion_signatures` metadata.
- `sender_to`: `connect(sender, receiver)` returns an operation state.
- `scheduler`: `schedule(scheduler)` returns a sender.
- `stop_token`: supports `stop_requested()` and callback registration.
- `stop_source`: supports `request_stop()`, `stop_requested()`, and
  `get_token()`.

The concepts are compatibility checks for this library, not complete P2300
semantic contracts.

## CPO And Member Customization

bexec uses CPO-style function objects, but the customization path is explicitly
member-based:

- `start(op)` calls `op.start()`.
- `connect(sender, receiver)` calls `sender.connect(receiver)`.
- `schedule(scheduler)` calls `scheduler.schedule()`.
- `set_value(receiver, args...)` calls `receiver.set_value(args...)`.
- `set_error(receiver, error)` calls `receiver.set_error(error)`.
- `set_stopped(receiver)` calls `receiver.set_stopped()`.
- `get_env(receiver)` calls `receiver.get_env()` when available.
- `query(env, tag)` calls `env.query(tag)`.

There is no `tag_invoke` support. This is intentional. It keeps overload
resolution easy to reason about, keeps diagnostics smaller, and avoids exposing
unstable implementation hooks while the library is still an MVP.

## Completion Metadata

`completion_signatures<ValueSignatures, ErrorTypes, SendsStopped>` is a small
metadata type, not the full P2300 model.

It supports:

- value signatures for simple senders such as `just`.
- declared error types for `when_all` aggregation.
- a boolean stopped-signal flag.

`then` transforms value signatures for simple invocable cases and adds
`std::exception_ptr` to possible errors when exceptions are enabled in normal
C++ builds.

## Ownership Model

Senders are lightweight values. `connect` consumes or copies a sender into an
operation state, depending on value category and copyability.

Receivers are moved into operation states. A receiver must remain valid until it
receives one terminal signal. The library does not retain references to a
receiver after terminal completion except where a user-provided receiver type
itself stores shared state.

Operation states are single-start objects. Starting an operation more than once
is outside the supported contract.

## Environment Model

The environment model is deliberately simple. A receiver may provide:

```cpp
auto get_env() const;
```

The returned environment answers queries through member functions:

```cpp
auto query(bexec::get_stop_token_t) const;
```

If a receiver has no `get_env()`, `empty_env` is used. `empty_env` answers
`get_stop_token` with `never_stop_token`.

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

## Scheduler Model

`io_context` owns a mutex-protected FIFO queue of `std::function<void()>`.
`post` and `enqueue` are thread-safe and return `false` if the context is
stopped.

`run_one()` executes one queued item if available. `run()` repeatedly calls
`run_one()` until the queue is empty or the context is stopped. This is simpler
than `asio::io_context`: there is no work guard and `run()` does not block
waiting for future work after the queue becomes empty.

`schedule(scheduler)` returns a sender that posts receiver completion to the
context. If the receiver's stop token is already requested, it completes with
`set_stopped()` without posting. The posted handler checks the stop token again
before delivering `set_value()`.

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

## when_all Shared State

`when_all` stores shared state containing:

- the final receiver,
- a remaining-child count,
- a mutex,
- an internal `inplace_stop_source`,
- the first terminal error/stopped state,
- an optional error `std::variant`.

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
stores either a result or an exception. `scheduler.schedule_awaitable()` resumes
the coroutine on the associated `io_context`.

The coroutine support is intentionally scoped to demonstrating scheduler
integration. `task<T>` is not a sender and does not provide a general-purpose
async runtime.
