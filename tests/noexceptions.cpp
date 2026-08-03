/**
 * @file tests/noexceptions.cpp
 * @brief -fno-exceptions compile/run validation of noexcept fast paths.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-08-03
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * This program is compiled with -fno-exceptions. If it builds and runs, every
 * noexcept branch exercised here contains no try/catch/throw: the value-
 * construction, user-callable, and connect/start paths selected by `if
 * constexpr` on the noexcept properties of template arguments are free of
 * exception-handling code. Components whose contract requires throwing (e.g.
 * sync_wait rethrowing an error) are intentionally not covered.
 */

#include <bexec/into_variant.hpp>
#include <bexec/just.hpp>
#include <bexec/let.hpp>
#include <bexec/on.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/receiver.hpp>
#include <bexec/repeat_until.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>

namespace {

struct recv {
  void set_value() noexcept {}
  template <class... Args>
  void set_value(Args&&...) noexcept {}
  template <class Error>
  void set_error(Error&&) noexcept {}
  void set_stopped() noexcept {}
};

}  // namespace

int main() {
  // then: noexcept fn branch.
  {
    auto s = bexec::just(1) | bexec::then([](int x) noexcept { return x + 1; });
    static_assert(
        bexec::completion_signatures_of_t<decltype(s)>::template count_of<
            bexec::set_error_t>() == 0,
        "noexcept then must not declare set_error");
    auto op = bexec::connect(std::move(s), recv{});
    bexec::start(op);
  }

  // let_value: noexcept fn + noexcept child connect.
  {
    auto s = bexec::just(1) |
             bexec::let_value([](int v) noexcept { return bexec::just(v); });
    static_assert(
        bexec::completion_signatures_of_t<decltype(s)>::template count_of<
            bexec::set_error_t>() == 0,
        "noexcept let_value must not declare set_error");
    auto op = bexec::connect(std::move(s), recv{});
    bexec::start(op);
  }

  // when_all: noexcept value storage + noexcept child connect.
  {
    auto s = bexec::when_all(bexec::just(1), bexec::just(2));
    static_assert(
        bexec::completion_signatures_of_t<decltype(s)>::template count_of<
            bexec::set_error_t>() == 0,
        "noexcept when_all must not declare set_error");
    auto op = bexec::connect(std::move(s), recv{});
    bexec::start(op);
  }

  // into_variant: noexcept value storage.
  {
    auto s = bexec::into_variant(bexec::just(1));
    static_assert(
        bexec::completion_signatures_of_t<decltype(s)>::template count_of<
            bexec::set_error_t>() == 0,
        "noexcept into_variant must not declare set_error");
    auto op = bexec::connect(std::move(s), recv{});
    bexec::start(op);
  }

  // starts_on: noexcept schedule connect.
  {
    bexec::run_loop loop;
    auto s = bexec::starts_on(loop.get_scheduler(), bexec::just(1));
    auto op = bexec::connect(std::move(s), recv{});
    bexec::start(op);
    loop.finish();
    loop.run();
  }

  // repeat_until: noexcept factory + noexcept predicate.
  {
    auto s = bexec::repeat_until([]() noexcept { return bexec::just(1); },
                                 []() noexcept { return true; });
    static_assert(
        bexec::completion_signatures_of_t<decltype(s)>::template count_of<
            bexec::set_error_t>() == 0,
        "noexcept repeat_until must not declare set_error");
    auto op = bexec::connect(std::move(s), recv{});
    bexec::start(op);
  }

  return 0;
}
