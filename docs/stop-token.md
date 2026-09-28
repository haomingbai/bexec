# Stop Token Model

`inplace_stop_source`, `inplace_stop_token`, and `inplace_stop_callback` provide
a small callback-based stop mechanism. Callback records are stored intrusively
inside `inplace_stop_callback`; registration does not allocate and the source
owns only a linked list head plus synchronization state.

The model follows the C++26 `std::inplace_stop_*` lifetime rule:
`inplace_stop_source` is the only owner of the stop state. Associated
`inplace_stop_token` and `inplace_stop_callback` objects do not extend that
state's lifetime, so all uses of those associated objects, including callback
deregistration during `inplace_stop_callback` destruction, must occur before
the associated `inplace_stop_source` is destroyed.

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
