# Usage

bexec is a C++20 header-only sender/receiver concurrency library inspired by
P2300. All public APIs are available through `#include <bexec/bexec.hpp>`.

## Architecture Overview

```mermaid
graph TB
    subgraph "Sender Factories"
        JUST["just(values...)"]
        JUST_E["just_error(error)"]
        JUST_S["just_stopped()"]
        SCHED["schedule(scheduler)"]
    end

    subgraph "Completion Adaptors"
        THEN["then(fn)"]
        UPON_E["upon_error(fn)"]
        UPON_S["upon_stopped(fn)"]
        INTO_V["into_variant()"]
    end

    subgraph "Continuation Adaptors"
        LET_V["let_value(fn)"]
        LET_E["let_error(fn)"]
        LET_S["let_stopped(fn)"]
    end

    subgraph "Algorithms"
        WHEN_ALL["when_all(...)"]
        REPEAT["repeat_until(factory, pred)"]
    end

    subgraph "Scheduling"
        RUN_LOOP["run_loop"]
        STARTS_ON["starts_on(sched, sender)"]
        ON["on(sched, sender)"]
        SYNC_WAIT["sync_wait(sender)"]
    end

    subgraph "Scopes"
        SCOPE["counting_scope"]
        ASSOCIATE["associate(sender, token)"]
        SPAWN["spawn(sender, token)"]
        SPAWN_F["spawn_future(sender, token)"]
    end

    subgraph "Infrastructure"
        CONNECT["connect(sender, receiver)"]
        START["start(op)"]
        ENV["get_env / query"]
        STOP["inplace_stop_*"]
    end

    subgraph "Coroutines"
        TASK["task<T> / generator<T><br/>co_await sender"]
    end

    JUST --> THEN
    JUST --> LET_V
    SCHED --> STARTS_ON
    SCHED --> ON
    THEN --> WHEN_ALL
    LET_V --> WHEN_ALL
    STARTS_ON --> SYNC_WAIT
    SCOPE --> ASSOCIATE
    SCOPE --> SPAWN
    SCOPE --> SPAWN_F
    CONNECT --> START
    ENV --> THEN
    ENV --> WHEN_ALL
    STOP --> ENV
    STOP --> WHEN_ALL
    STOP --> SCOPE
```

## Quick Start

```cpp
#include <bexec/bexec.hpp>

struct receiver {
    void set_value(int value) noexcept { /* use value */ }
    void set_error(std::exception_ptr error) noexcept { /* handle error */ }
    void set_stopped() noexcept { /* handle cancellation */ }
};

auto s = bexec::just(1) | bexec::then([](int x) { return x + 1; });
auto op = bexec::connect(std::move(s), receiver{});
bexec::start(op);
```

## Documentation Index

| Document | Contents |
|----------|----------|
| [basic-sender-receiver.md](basic-sender-receiver.md) | Sender/receiver fundamentals, connect/start flow, receiver contract |
| [sender-factories.md](sender-factories.md) | `just`, `just_error`, `just_stopped` sender factories |
| [completion-adaptors.md](completion-adaptors.md) | `then`, `upon_error`, `upon_stopped` completion adaptors, `into_variant` |
| [let-adaptors.md](let-adaptors.md) | `let_value`, `let_error`, `let_stopped` continuation adaptors |
| [scheduling.md](scheduling.md) | `run_loop` scheduler, `starts_on`/`on`, `schedule`, `sync_wait` |
| [stop-tokens.md](stop-tokens.md) | `inplace_stop_source`, `inplace_stop_token`, `inplace_stop_callback` |
| [environments.md](environments.md) | Environment queries: `get_env`, `get_stop_token`, `get_allocator`, `get_scheduler`, environment wrappers |
| [concepts-and-metadata.md](concepts-and-metadata.md) | Concept constraints, completion signature introspection |
| [algorithms.md](algorithms.md) | `when_all`, `when_all_with_variant`, `repeat_until` |
| [counting-scopes.md](counting-scopes.md) | `counting_scope`, `simple_counting_scope`, `associate`, `spawn`, `spawn_future` |
| [coroutines.md](coroutines.md) | Sender awaiting, `task<T>`, and synchronous `generator<T>` |

## Other Documentation

- [README.md](../../README.md) — Project overview, build instructions, limitations
- [design.md](../design.md) — Design rationale and architectural decisions
- [maintenance.md](../maintenance.md) — Maintainer guide (code layout, naming, how to add features)
- [roadmap.md](../roadmap.md) — Planned improvements
