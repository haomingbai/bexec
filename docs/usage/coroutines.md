# Coroutines

`task<T>` is a small lazy coroutine task helper. It starts lazily (execution
begins only when `start()` is called) and stores either a result or an exception.

## `task<T>`

```cpp
#include <bexec/task.hpp>

bexec::task<int> compute_value() {
    co_return 42;
}

auto task = compute_value();

// task is lazy; execution has not started yet
task.start();              // start/resume the coroutine
int value = task.result(); // retrieve the result (may throw stored exception)
```

## `task<void>`

The void specialization stores no value, only a possible exception:

```cpp
bexec::task<void> do_work() {
    // perform some work without producing a value
    co_return;
}

auto t = do_work();
t.start();
t.result();  // returns void; may throw stored exception
```

## Lifecycle

- **Lazy start**: The coroutine begins execution only on the first `start()`
  call.
- **Result consumption**: `result()` returns the stored value (or rethrows the
  stored exception). `task<T>::result()` returns `T`; `task<void>::result()`
  returns `void`.
- **Done check**: `done()` returns `true` when the coroutine has reached its
  final suspend point.
- **Move-only**: Tasks are move-only; they cannot be copied.
- **Destruction**: If a task is destroyed before completion, the coroutine frame
  is destroyed (the coroutine is cancelled).
- **Exceptions**: When exceptions are enabled, unhandled exceptions in the
  coroutine are stored as `std::exception_ptr` and rethrown by `result()`.

## Limitations

`task<T>` is intentionally minimal:

- It is **not** a sender. It cannot be composed with sender pipelines through
  `connect`/`start`.
- It is **not** a general-purpose coroutine framework.
- It does **not** make scheduler senders directly awaitable.

Receiver-based coroutine integration (making `co_await schedule(scheduler)`
possible) is tracked in [roadmap.md](../roadmap.md).
