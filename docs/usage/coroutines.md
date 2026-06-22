# Coroutines

bexec provides three coroutine building blocks:

- `task<T>` for lazy, single-result work.
- `generator<T>` for synchronous single-pass iteration.
- `as_awaitable` and `with_awaitable_senders` for awaiting senders from a
  compatible promise.

## Awaiting Senders

`task<T>` promises derive from `with_awaitable_senders`, so a sender with no
value or one value can be awaited directly:

```cpp
#include <bexec/bexec.hpp>

bexec::task<int> compute(bexec::run_loop& loop) {
    co_await bexec::schedule(loop.get_scheduler()); // set_value()
    int value = co_await bexec::just(42);           // set_value(int)
    co_return value;
}
```

The supported successful completion shapes are:

- no `set_value` completion, or one `set_value()` completion: `co_await`
  returns `void`;
- one `set_value(T)` completion: `co_await` returns `std::decay_t<T>`.

Senders with multiple value alternatives or multiple values in one completion,
such as `set_value(int, std::string)`, are not awaitable through this bridge.

`set_error(error)` is converted to `std::exception_ptr` and rethrown from
`await_resume()`. `set_stopped()` invokes the promise's
`unhandled_stopped()` path and does not resume the stopped coroutine body.

The sender operation is stored directly in the coroutine awaiter. This supports
non-movable operation states and performs no separate operation allocation.
The C++ implementation may still allocate the coroutine frame itself.

## `as_awaitable`

```cpp
auto awaitable = bexec::as_awaitable(value, promise);
```

`as_awaitable` uses this order:

1. `value.as_awaitable(promise)`, when available;
2. an already awaitable value, unchanged;
3. a supported sender, converted to
   `sender_awaitable<Sender, Promise>`;
4. any other value, unchanged so normal coroutine diagnostics apply.

Custom promise types can inherit the mixin:

```cpp
struct promise_type : bexec::with_awaitable_senders<promise_type> {
    // normal coroutine promise members
};
```

The additional continuation members on `with_awaitable_senders` are not
bexec-specific task conveniences. They follow the shape specified for
[`execution::with_awaitable_senders` in P2300R10](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2300r10.html#exec.with.awaitable.senders):

- `set_continuation` records the calling coroutine and installs a type-erased
  stopped handler for its promise type;
- `continuation` exposes that handle for normal symmetric transfer at final
  suspend;
- `unhandled_stopped` invokes the stopped handler so cancellation propagates to
  the caller's promise without resuming the cancelled coroutine.

This state is required for P2300-style stopped propagation across a coroutine
call chain. A mixin containing only `await_transform` could await successful and
error-completing senders, but each coroutine type would then need to implement
its own stopped propagation protocol. P2300 specifies termination when no
caller-side stopped handler exists; bexec expresses that invalid path using the
project's debug-assert plus unreachable convention.

The receiver used by `sender_awaitable` exposes the promise environment through
`get_env()`. A promise can therefore provide scheduler, stop-token, allocator,
or application-specific environment queries to an awaited sender.

## `task<T>`

Tasks remain lazy and move-only:

```cpp
auto task = compute(loop);
task.start();

loop.finish();
loop.run();

int value = task.result();
```

Tasks can also await temporary child tasks:

```cpp
bexec::task<int> parent(bexec::run_loop& loop) {
    int value = co_await compute(loop);
    co_return value + 1;
}
```

Only rvalue tasks are awaitable because awaiting consumes ownership of the child
coroutine. `task<void>` follows the same rules without a returned value.

`result()` rethrows an unhandled exception. If the task completed through
`set_stopped`, it throws `bexec::task_stopped`.

### Task Lifetime

- A task waiting for a sender or child task is resumed by that operation. Do
  not call `start()` again while it is waiting.
- A task must remain alive until an awaited sender sends a terminal completion.
  Destroying an operation state while its asynchronous work is outstanding is
  outside the sender/receiver lifetime contract.
- These are API lifetime preconditions rather than runtime state tracked by
  `task`. Automatic stop requests and shared task lifetime are not provided.
- Manual repeated `start()` remains supported for native awaiters such as
  `std::suspend_always`, preserving the original task behavior.

## `generator<T>`

`generator<T>` is a synchronous, move-only `std::ranges::input_range`:

```cpp
bexec::generator<int> values() {
    co_yield 1;
    co_yield 2;
    co_yield 3;
}

for (int value : values()) {
    // 1, 2, 3
}
```

Its iterator dereferences to `const T&` and compares with
`std::default_sentinel`. It works with range-for and ranges algorithms such as
`std::ranges::copy`.

The generator is single-pass and may be started only once. It supports object
element types and synchronous `co_yield`; `co_await` is deliberately disabled.
Exceptions raised by the generator body are rethrown by iteration.
