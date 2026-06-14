# Algorithms

## `when_all`

`when_all(a, b, ...)` starts all child senders and completes after all started
children have finished.

### Execution Model

```mermaid
sequenceDiagram
    participant WA as when_all_state
    participant SS as Shared Stop Source
    participant C1 as Child 1
    participant C2 as Child 2
    participant C3 as Child 3
    participant DR as Downstream Receiver

    WA->>SS: create shared stop source
    WA->>C1: start(child_1)
    WA->>C2: start(child_2)
    WA->>C3: start(child_3)

    C1-->>WA: set_value(v1...) ✓
    C2-->>WA: set_value(v2...) ✓
    C3-->>WA: set_error(e) ❌ (first error)

    WA->>SS: request_stop()
    Note over C1,C2: Already completed, no effect
    Note over C3: Already completed, no effect

    WA->>DR: set_error(e)
```

```mermaid
stateDiagram-v2
    [*] --> Starting: start()
    Starting --> Waiting: all children started
    state Waiting {
        [*] --> Collecting
        Collecting --> FirstError: child set_error(e)
        Collecting --> FirstStopped: child set_stopped()
        Collecting --> AllSuccess: all set_value(...)

        FirstError --> Draining: request_stop()
        FirstStopped --> Draining: request_stop()

        Draining --> DrainDone: all children finished
    }
    AllSuccess --> Done: set_value(concat...)
    DrainDone --> Done: set_error(e) / set_stopped()
    Done --> [*]
```

### Basic Usage

```cpp
bexec::run_loop loop;
auto sched = loop.get_scheduler();

auto a = bexec::schedule(sched) | bexec::then([] {});
auto b = bexec::schedule(sched) | bexec::then([] {});

auto op = bexec::connect(bexec::when_all(std::move(a), std::move(b)), receiver{});
bexec::start(op);
loop.finish();
loop.run();
```

### All-Success Completion

All-success completion sends concatenated child values in argument order:

```cpp
auto result = bexec::this_thread::sync_wait(
    bexec::when_all(bexec::just(1, 2), bexec::just(std::string{"ok"})));

// result has type std::optional<std::tuple<int, int, std::string>>
// result contains tuple{1, 2, "ok"}
```

### Error and Stopped Handling

On the first error or stopped signal, `when_all` requests stop through its
internal stop source and waits for all started children to finish before
completing the receiver. Errors are delivered as their original error type;
`std::exception_ptr` is also listed to cover internal connect/start failures.

If the receiver environment has a stoppable token, requesting that token also
requests stop for all child senders through the `when_all` environment.

### `when_all` vs `when_all_with_variant`

Plain `when_all` requires each child sender to have at most one value completion
alternative. Use `when_all_with_variant` for senders with multiple value
alternatives:

```cpp
auto s = bexec::when_all_with_variant(maybe_int_or_string(), bexec::just(3));
```

`when_all_with_variant` applies `into_variant` to each child before passing to
`when_all`.

### Limitations

`when_all()` and `when_all_with_variant()` with zero senders are ill-formed.

## `repeat_until`

`repeat_until(factory, predicate)` repeatedly creates and starts a fresh child
sender. After each successful child completion, `predicate()` is called. When the
predicate returns `true`, the repeat sender completes with `set_value()`.

### Execution Model

```mermaid
flowchart TD
    START([start]) --> CHECK_STOP{"stop requested?"}
    CHECK_STOP -->|yes| STOPPED["set_stopped() 🛑"]
    CHECK_STOP -->|no| FACTORY["factory() → new sender"]
    FACTORY --> CONNECT["connect(sender, child_receiver)"]
    CONNECT --> START_CHILD["start(child_op)"]

    START_CHILD --> SYNC{"synchronous completion?"}
    SYNC -->|yes| PRED["predicate()"]
    SYNC -->|no| WAIT["wait for async callback"]
    WAIT --> PRED

    PRED -->|true| DONE["set_value() ✨"]
    PRED -->|false| CHECK_STOP

    START_CHILD -.->|error| ERROR["set_error(e) ❌"]
    START_CHILD -.->|stopped| STOPPED
```

```cpp
int count = 0;

auto repeated = bexec::repeat_until(
    [&] {
        return bexec::just() | bexec::then([&] { ++count; });
    },
    [&] { return count == 10; });

auto op = bexec::connect(std::move(repeated), receiver{});
bexec::start(op);
// After 10 iterations, receiver receives set_value()
```

### Design Notes

Child values are discarded. The factory form is intentional: it avoids restarting
the same operation state (which would be invalid for many senders), and works for
move-only senders.

The implementation uses a trampoline so synchronous children such as `just()` do
not recursively call `start()` and do not grow the stack per iteration.

### Error and Stop Propagation

Child errors and stopped signals are propagated directly to the receiver.
Cancellation is checked through the receiver environment before each iteration.
