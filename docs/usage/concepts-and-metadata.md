# Concepts And Metadata

bexec provides C++20 concepts for constraining sender, receiver, and other roles
in template code, along with completion signature introspection utilities for
compile-time inspection and transformation of sender completion signatures.

## Concepts

All concepts are available through `#include <bexec/concepts.hpp>`.

### Core Role Concepts

```cpp
#include <bexec/concepts.hpp>

// Constrain a function to accept only sender types
template <bexec::sender Sender>
void my_algorithm(Sender&& sender) {
    auto op = bexec::connect(std::forward<Sender>(sender), my_receiver{});
    bexec::start(op);
}
```

Available concepts:

| Concept | Header | Checks |
|---------|--------|--------|
| `bexec::sender<S>` | `include/bexec/sender.hpp` | Move-constructible, exposes completion_signatures metadata |
| `bexec::sender_in<S, Env>` | `include/bexec/sender.hpp` | sender + environment-aware completion signatures are valid |
| `bexec::sender_to<S, R>` | `include/bexec/sender.hpp` | `connect(sender, receiver)` returns an operation_state |
| `bexec::receiver<R>` | `include/bexec/receiver.hpp` | Move-constructible |
| `bexec::receiver_of<R, Completions>` | `include/bexec/receiver.hpp` | receiver accepts the given completion signature set |
| `bexec::operation_state<Op>` | `include/bexec/operation_state.hpp` | `start(op)` is valid |
| `bexec::scheduler<S>` | `include/bexec/scheduler.hpp` | Copyable, equality-comparable, `schedule()` returns a sender |
| `bexec::stop_token<T>` | `include/bexec/stop_token.hpp` | Copyable, `stop_requested()`, callback type |
| `bexec::stop_source<S>` | `include/bexec/stop_token.hpp` | Movable, `request_stop()`, `stop_requested()`, `get_token()` |
| `bexec::completion_signature<S>` | `include/bexec/completion_signatures.hpp` | A valid completion signature function type |
| `bexec::valid_completion_signatures<C>` | `include/bexec/completion_signatures.hpp` | Type is a valid `completion_signatures` instantiation |

### Using Concepts to Constrain Templates

```cpp
// Constrain: T must be a sender whose value completion yields int
template <bexec::sender Sender>
  requires std::same_as<
      bexec::value_types_of_t<Sender>,
      bexec::variant_or_empty<std::tuple<int>>>
void consume_int(Sender&& sender) {
    // ...
}

// Constrain: S must be a scheduler
template <bexec::scheduler Sched>
void run_on(Sched scheduler, bexec::sender auto work) {
    auto s = bexec::starts_on(scheduler, std::move(work));
    // ...
}
```

Scope-related concepts:

```cpp
#include <bexec/counting_scope.hpp>

template <bexec::scope_token Token>
void launch_work(Token token, bexec::sender auto work) {
    bexec::spawn(std::move(work), token);
}
```

The concepts are compatibility checks for this library, not complete P2300
semantic contracts.

## Completion Signature Introspection

```cpp
#include <bexec/completion_signatures.hpp>
#include <bexec/sender.hpp>
```

### Querying a Sender's Completion Signatures

```cpp
using my_sigs = bexec::completion_signatures_of_t<MySender>;
// my_sigs is completion_signatures<set_value_t(int), set_error_t(std::exception_ptr), ...>
```

Environment-aware form (accounts for receiver environment influence on
signatures):

```cpp
using env_sigs = bexec::completion_signatures_of_t<MySender, MyEnv>;
```

Senders may optionally provide a static member function template
`get_completion_signatures<Sender, Env...>()` to declare environment-aware
signatures; otherwise the library falls back to the nested
`completion_signatures` member type alias.

### Extracting Value Completion Types

```cpp
using values = bexec::value_types_of_t<MySender>;
// values is variant_or_empty<std::tuple<int>, std::tuple<std::string>>
// multiple alternatives → variant; one alternative → may be single tuple; zero → type_list<>
```

Environment-aware:

```cpp
using values = bexec::value_types_of_t<MySender, MyEnv>;
```

### Extracting Error Types

```cpp
using errors = bexec::error_types_of_t<MySender>;
// errors is variant_or_empty<std::exception_ptr, std::error_code>
// error types are extracted as individual types (not wrapped in tuples)
```

Environment-aware:

```cpp
using errors = bexec::error_types_of_t<MySender, MyEnv>;
```

### Checking for Stopped

```cpp
constexpr bool can_stop = bexec::sends_stopped<MySender>;
// or environment-aware:
constexpr bool can_stop_env = bexec::sends_stopped<MySender, MyEnv>;
```

### The `completion_signatures` Type

```cpp
using sigs = bexec::completion_signatures<
    bexec::set_value_t(int),
    bexec::set_value_t(std::string),
    bexec::set_error_t(std::exception_ptr),
    bexec::set_stopped_t()>;
```

`completion_signatures` provides compile-time introspection:

```cpp
// Count occurrences of a specific completion kind
constexpr std::size_t value_count = sigs::count_of<bexec::set_value_t>();  // 2
constexpr std::size_t error_count = sigs::count_of<bexec::set_error_t>();  // 1

// Iterate over all signatures (useful for testing or debugging)
sigs::for_each([]<class Sig>(Sig*) {
    // Sig is set_value_t(int), set_value_t(std::string), ...
});
```

### Utility Type Aliases

| Type | Purpose |
|------|---------|
| `bexec::variant_or_empty<Ts...>` | `std::variant<Ts...>` when non-empty, otherwise `type_list<>` |
| `bexec::single_type<T>` | Extracts identity from a single type (used for error type extraction) |
