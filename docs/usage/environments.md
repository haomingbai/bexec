# Environments

The environment model allows receivers to expose a query interface through which
algorithms and adaptors retrieve contextual information such as stop tokens,
allocators, and schedulers.

## Basic Queries

```cpp
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>

auto env = bexec::get_env(receiver);
auto token = bexec::query(env, bexec::get_stop_token);
auto same_token = bexec::get_stop_token(env);  // equivalent shorthand
auto allocator = bexec::query(env, bexec::get_allocator);
```

## Query Tags

| Query tag | Shorthand | Purpose |
|-----------|-----------|---------|
| `bexec::get_stop_token` | `bexec::get_stop_token(env)` | Retrieve the stop token |
| `bexec::get_allocator` | `bexec::get_allocator(env)` | Retrieve the allocator; defaults to `std::allocator<std::byte>` |
| `bexec::get_scheduler` | `bexec::get_scheduler(env)` | Retrieve the scheduler (no fallback) |
| `bexec::get_delegation_scheduler` | `bexec::get_delegation_scheduler(env)` | Retrieve the delegation scheduler (no fallback) |

## `empty_env`

If a receiver has no `get_env()` member, `bexec::get_env(receiver)` returns
`empty_env`. `empty_env` provides the following fallbacks:

- `get_stop_token` → `never_stop_token` (never requests stop)
- `get_allocator` → `std::allocator<std::byte>`

## `env_with_stop_token`

`env_with_stop_token<BaseEnv>` overrides the `get_stop_token` query and delegates
other queries to the wrapped environment. `BaseEnv` defaults to `empty_env`.

```cpp
bexec::inplace_stop_source source;
bexec::env_with_stop_token<> env{source.get_token()};

auto token = bexec::get_stop_token(env);
// token is the inplace_stop_token from source
```

Using it in a custom receiver:

```cpp
class stop_receiver {
 public:
  explicit stop_receiver(bexec::inplace_stop_token token) : env_(token) {}

  void set_value() noexcept { /* ... */ }
  void set_error(std::exception_ptr) noexcept { /* ... */ }
  void set_stopped() noexcept { /* ... */ }

  bexec::env_with_stop_token<> get_env() const noexcept { return env_; }

 private:
  bexec::env_with_stop_token<> env_;
};
```

`when_all` internally uses `env_with_stop_token` to give all child senders a
shared cancellation token.

## `env_with_scheduler`

`env_with_scheduler<Scheduler, BaseEnv>` overrides the `get_scheduler` and
`get_delegation_scheduler` queries while delegating other queries.

```cpp
bexec::run_loop loop;
bexec::env_with_scheduler<bexec::run_loop::scheduler> env{
    loop.get_scheduler()};

auto sched = bexec::get_scheduler(env);
// sched is loop.get_scheduler()
```

Scheduling adaptors (`starts_on`, `on`) internally use `env_with_scheduler` to
provide scheduler-aware environments for child operations. Algorithm implementors
can use it when constructing child receiver environments.

## Query Model

The query model is member-based:

```cpp
auto env = receiver.get_env();                        // if present
auto token = env.query(bexec::get_stop_token);         // member query
auto same = bexec::get_stop_token(env);                // CPO shorthand
```

`bexec::query(env, tag)` calls `tag(env)`, which in turn calls the appropriate
query method on the environment. When an environment does not provide a query,
the query tag can offer a fallback (e.g., `get_allocator` falls back to
`std::allocator<std::byte>`).
