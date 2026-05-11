# Progress

## Checkpoint 1: Project scaffold

- Completed: created a CMake-based C++20 interface-library scaffold.
- Files changed:
  - `CMakeLists.txt`
  - `tests/CMakeLists.txt`
  - `examples/CMakeLists.txt`
  - `PROGRESS.md`
- Tests run:
  - `cmake -S . -B build`
- Known limitations/TODOs:
  - Library implementation, tests, examples, and documentation still need to be added.

## Checkpoint 2: Core MVP implementation

- Completed:
  - Added the public header `include/bexec/bexec.hpp`.
  - Implemented member-based CPOs for `start`, `connect`, `schedule`, receiver completions, `get_env`, and `query`.
  - Implemented minimal completion-signature metadata.
  - Implemented `just`, `just_error`, `just_stopped`, `then`, `repeat_until`, `when_all`, `io_context`, stop token/source/callback, environment helpers, and coroutine `task<T>`.
  - Added a CTest executable with coverage for core concepts, factories, adaptors, scheduler behavior, stop callbacks, env/query, repeat, when_all, and coroutine scheduling.
  - Added a compiled example executable.
- Files changed:
  - `include/bexec/bexec.hpp`
  - `tests/CMakeLists.txt`
  - `tests/test_bexec.cpp`
  - `examples/CMakeLists.txt`
  - `examples/basic.cpp`
  - `PROGRESS.md`
- Tests run:
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure`
- Known limitations/TODOs:
  - Documentation still needs to be written.
  - `when_all` currently discards child success values and completes with `set_value()`.
  - `repeat_until` intentionally uses a sender factory and discards child values.
  - The scheduler `run()` drains queued work and returns when the queue is empty; it is not a work-guarded blocking event loop.
