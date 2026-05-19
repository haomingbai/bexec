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
`error_types_of_t`, and `sends_stopped` inspect this signature pack. `then`,
`upon_error`, and `upon_stopped` transform the selected completion kind into a
value completion and add `std::exception_ptr` to possible errors. `let_value`,
`let_error`, and `let_stopped` replace the selected upstream completion
signatures with the completion signatures of the sender returned by the user
callable, preserve non-selected completions, and add `std::exception_ptr` for
callable/connect exceptions.

`into_variant` maps value signatures to one
`set_value_t(std::variant<std::tuple<...>, ...>)` signature, with duplicate
tuple alternatives removed. Plain `when_all` requires each child sender to have
at most one value completion and declares concatenated successful values plus
raw child error alternatives. `when_all_with_variant` applies `into_variant` to
each child before `when_all`.

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
auto query(bexec::get_scheduler_t) const noexcept;
auto query(bexec::get_delegation_scheduler_t) const noexcept;
```

If a receiver has no `get_env()`, `empty_env` is used. `empty_env` answers
`get_stop_token` with `never_stop_token` and `get_allocator` with
`std::allocator<std::byte>`.

`env_with_stop_token<BaseEnv>` overrides `get_stop_token` and delegates other
queries to the wrapped environment. `when_all` uses this to give children a
shared cancellation token.

`env_with_scheduler<Scheduler, BaseEnv>` overrides `get_scheduler` and
`get_delegation_scheduler` while delegating other queries. Scheduling adaptors
use scheduler-aware environments so child operations can observe the execution
resource they are running on.

## Stop Token Model

`inplace_stop_source`, `inplace_stop_token`, and `inplace_stop_callback` provide
a small callback-based stop mechanism. Callback records are stored intrusively
inside `inplace_stop_callback`; registration does not allocate and the source
owns only a linked list head plus synchronization state.

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

## run_loop Model

`run_loop` is a stack-owned FIFO scheduler intended for local execution,
`sync_wait`, and tests. Scheduled operations derive from a small intrusive
operation node. Starting a schedule operation links that operation into the
loop queue; no `std::function` or heap allocation is needed for the scheduling
path.

`run()` blocks until `finish()` is requested and queued work has drained.
`run_one()` executes one queued item if available. `sync_wait` uses a local
`run_loop` and a receiver environment that answers both `get_scheduler` and
`get_delegation_scheduler` with that loop's scheduler.

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

`io_context::post(std::function<void()>)` is a demonstration API and may
allocate. The newer operation-state algorithms avoid adding heap ownership of
their own.

## Scheduling Adaptors And sync_wait

`starts_on(scheduler, sender)` first starts `schedule(scheduler)`. When that
scheduling sender completes successfully, it connects and starts the child
sender. Schedule errors and stopped signals are forwarded to the downstream
receiver.

`on(scheduler, sender)` starts the child through `starts_on`, stores the child
completion in-place, then schedules final delivery through
`get_scheduler(get_env(receiver))`. Connecting an `on` sender is ill-formed
when that scheduler query is unavailable.

`bexec::this_thread::sync_wait(sender)` connects the sender to a receiver backed
by a local `run_loop`. Value completion returns
`std::optional<std::tuple<...>>`, stopped returns `std::nullopt`, and errors are
thrown. `sync_wait_with_variant` applies `into_variant` and returns
`std::optional<std::variant<std::tuple<...>, ...>>`.

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
- an optional error `std::variant`,
- in-place optional storage for each child value tuple.

The state is a direct member of the operation state. Child receivers store a
pointer to it; they do not share-own it. This relies on the P2300 operation
lifetime rule that the operation state remains alive until completion.

All child operations are started. On the first error or stopped signal,
`when_all` records that terminal state and requests stop through the shared stop
source. Children receive that token through their environment.

The final receiver is completed only after all started children have finished.
If the first terminal state was an error, the receiver gets the stored error as
its original error type. If it was stopped, the receiver gets `set_stopped()`.
If all children succeed, the stored value tuples are concatenated and delivered
as `set_value(args...)` in child order.

The internal error storage remains a `std::variant` so the operation can keep
the first error until all children finish. That variant is not the public error
completion shape.

## Coroutine Design

`task<T>` is a lazy coroutine task. It starts when `start()` is called and
stores either a result or an exception.

The coroutine support is intentionally scoped to a minimal task helper.
Schedulers expose scheduling through `schedule(scheduler)`, not through
awaiter member functions. Future sender-to-awaitable support should be built
with an internal receiver that connects to a sender and resumes the coroutine
from receiver completion signals.
