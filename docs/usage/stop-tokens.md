# Stop Tokens

bexec provides `inplace_stop_source`, `inplace_stop_token`, and
`inplace_stop_callback` for cooperative cancellation. These types follow the
C++26 `std::inplace_stop_*` model.

## Basic Usage

```cpp
bexec::inplace_stop_source source;
auto token = source.get_token();

bexec::inplace_stop_callback cb{token, [] {
    // Called when stop is requested. Invoked at most once.
}};

source.request_stop();
```

## Lifetime Rules

`inplace_stop_source` is the sole owner of the stop state. Associated
`inplace_stop_token` and `inplace_stop_callback` objects do not extend that
state's lifetime. All uses of those associated objects, including callback
deregistration during `inplace_stop_callback` destruction, must occur before the
associated `inplace_stop_source` is destroyed.

This means:

- Destroy `inplace_stop_callback` objects before `inplace_stop_source`.
- Do not use a token after its `inplace_stop_source` has been destroyed.

## Thread Safety Guarantees

- `request_stop()` is thread-safe.
- `stop_requested()` is thread-safe.
- Callback registration is thread-safe relative to `request_stop()`.
- If stop has already been requested, registration invokes the callback
  immediately.
- Callback invocation is one-shot.
- Destroying a callback registration prevents future invocation if the callback
  has not already been selected for invocation.

## Callback Behavior

Callbacks are expected not to throw. If a callback throws, the implementation
terminates.

## `never_stop_token`

`never_stop_token` is a token that never requests stop. Receivers without a
receiver environment use it through the `empty_env` fallback. See
[environments.md](environments.md).
