# Sender Factories

Sender factories create senders that complete immediately with predefined
results.

## `just`

`just(values...)` completes synchronously with `set_value(values...)`.

```cpp
auto op = bexec::connect(bexec::just(1, std::string{"hello"}), receiver{});
bexec::start(op);
// receiver receives set_value(1, std::string{"hello"})
```

Move-only values are supported when the sender is connected as an rvalue:

```cpp
auto op = bexec::connect(
    bexec::just(std::make_unique<int>(42)),
    receiver{});
bexec::start(op);
// receiver receives set_value(std::unique_ptr<int>)
```

## `just_error`

`just_error(error)` completes with `set_error(error)`.

```cpp
auto error_op = bexec::connect(
    bexec::just_error(std::string{"failed"}),
    receiver{});
bexec::start(error_op);
// receiver receives set_error(std::string{"failed"})
```

## `just_stopped`

`just_stopped()` completes with `set_stopped()`.

```cpp
auto stopped_op = bexec::connect(
    bexec::just_stopped(),
    receiver{});
bexec::start(stopped_op);
// receiver receives set_stopped()
```
