# Roadmap

This roadmap tracks intentional gaps between the MVP and a fuller
P2300-inspired library.

## Completion Signatures

- Expand coverage of P2300-style completion-signature transformations as more
  adaptors are added.
- Improve compile-time diagnostics when adaptor callables are not invocable for
  all `set_value_t(Args...)` alternatives.

## Cancellation

- Expand stop-token integration across all algorithms.
- Add tests for races between stop request, callback destruction, and
  asynchronous completion.
- Consider cancellation-aware scheduler queues.

## Scheduler Features

- Add a blocking `run_forever()` or work-guard model if real applications need
  a long-running event loop.
- Add timer support.
- Add scheduler environment queries and properties beyond the current
  `get_scheduler` / `get_delegation_scheduler` subset.

## Coroutines

- Add receiver-based sender awaitable integration so scheduling can be written
  as `co_await schedule(scheduler)`.
- Decide whether `task<T>` should become a sender.
- Add cancellation propagation into coroutine tasks.
- Add async composition helpers.

## Allocators And ABI

- Use `get_allocator` for any future operation-state heap storage.
- Keep the public ABI header-only until a stable boundary is justified.

## P2300 Compatibility

- Track naming and semantic differences against the standard wording.
- Add unimplemented standard algorithms such as `bulk`, `split`,
  `stopped_as_optional`, and public `schedule_from` when they are needed.
- Add migration notes once mainstream C++26 execution implementations become
  available.
