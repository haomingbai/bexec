# Counting Scopes

Counting scopes track outstanding associated work. `simple_counting_scope` tracks
an association count; `counting_scope` adds cooperative cancellation via
`request_stop()`.

## Scope State Machine

```mermaid
stateDiagram-v2
    [*] --> open

    open --> open_joining: join() started
    open --> closed: close()
    open_joining --> closed_joining: close() or count == 0
    closed --> closed_joining: join() started and count > 0
    closed --> joined: join() started and count == 0
    closed_joining --> joined: count == 0

    note right of open: Can accept work
    note right of open_joining: Can accept work while join waits
    note right of closed: New work rejected<br/>Existing work still valid
    note right of joined: Scope can be safely destroyed

    joined --> [*]
```

### Creating a Scope and Token

```cpp
bexec::counting_scope scope;
auto token = scope.get_token();
```

Work is associated with the scope through a scope token.

## `spawn`

`spawn(sender, token)` eagerly starts detached work and keeps the scope
associated until the child completes. Detached spawned senders must complete only
with `set_value()` and/or `set_stopped()`.

```mermaid
sequenceDiagram
    participant U as User
    participant SC as Scope
    participant SP as spawn
    participant H as Heap (spawn_operation)
    participant CH as Child Sender

    U->>SP: spawn(sender, token)
    SP->>SC: try_associate() → ✓
    SP->>H: allocate (via get_allocator)
    SP->>CH: wrap + connect + start
    Note over H,CH: Detached, eagerly started

    CH-->>H: set_value() / set_stopped()
    H->>SC: disassociate()
    H->>H: destroy + deallocate
```

```cpp
bexec::spawn(bexec::just() | bexec::then([] {
    // Fire-and-forget work
}), token);
```

## `spawn_future`

`spawn_future(sender, token)` eagerly starts the input sender and returns a
move-only sender that later consumes the stored result:

```mermaid
sequenceDiagram
    participant U as User
    participant SF as spawn_future
    participant ST as Future State (heap)
    participant CH as Child Sender

    U->>SF: spawn_future(sender, token)
    SF->>ST: allocate + construct child op
    ST->>ST: try_associate()

    alt Association OK
        ST->>CH: start(child)
        SF-->>U: future_sender
        U->>ST: sync_wait / connect + start
        CH-->>ST: terminal signal
        ST-->>U: result
    else Scope closed/joined
        SF-->>U: future_sender (stores set_stopped())
        U->>ST: sync_wait / connect + start
        ST-->>U: nullopt (stopped)
    end

    alt Abandon (destroy future before complete)
        U->>ST: ~future_sender()
        ST->>CH: request_stop()
        Note over ST: Association held until child completes
    end
```

```cpp
auto future = bexec::spawn_future(bexec::just(42), token);
auto result = bexec::this_thread::sync_wait(std::move(future));

if (result) {
    auto [value] = *result;
}
```

### spawn_future Behavior Details

- If the scope is already closed or joined, `spawn_future` does not start the
  input sender and the returned sender completes with `set_stopped()`.
- Destroying the returned future sender before the child completes **abandons the
  future and requests stop** for the child; however, the scope association is
  still released only when the child eventually completes.
- Abandoning the future does not complete the future receiver with stopped. It
  only requests stop for the child.

## Scope Lifecycle

### `close()`

Prevents new associations. Existing work remains valid.

### `join()`

Returns a sender that completes when the association count reaches zero. Starting
a join does **not** immediately close a non-empty open scope to new work; the
scope still accepts associations while it is `open_joining`. Once the count
reaches zero, the join path closes the scope before completing.

```cpp
scope.close();
auto result = bexec::this_thread::sync_wait(scope.join());
```

The receiver used with `join()` must expose `get_scheduler` through its
environment; `this_thread::sync_wait(scope.join())` satisfies this requirement.

### Destruction

Destroying a scope while it has outstanding associations or a pending join
terminates the program.
Callers must close and join scopes that have accepted work before destroying them.

## `simple_counting_scope` vs `counting_scope`

- `simple_counting_scope` only tracks the association count (`close()`, `join()`).
- `counting_scope` additionally owns an `inplace_stop_source`; its token wraps
  child senders so scope stop requests are visible through `get_stop_token` to
  children. Call `request_stop()` to trigger cooperative cancellation.

```cpp
bexec::counting_scope scope;        // with cancellation support
bexec::simple_counting_scope scope; // count only
```
