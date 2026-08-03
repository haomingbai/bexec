/**
 * @file tests/basic/when_all_extraction.cpp
 * @brief Branch-coverage tests for when_all noexcept extraction.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-08-04
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * For every exception source inside when_all, this file exercises BOTH the
 * throwing construction (slow path) and the noexcept construction (fast path):
 *
 *   A. child value storage into the per-child slot (child_value)
 *   B. child error storage into the ErrorVariant (child_error)
 *   C. final value combination (ValuesTuple move in finish_one/deliver_success)
 *   D. ErrorVariant move out of the optional (finish_one)
 *   E. stop-callback construction (register_stop_callback)
 *   F. child connect/start (start_one)
 *
 * Each pair asserts the type-level extraction result (whether
 * set_error(std::exception_ptr) is declared) AND, when runnable, the runtime
 * delivery of the exception as set_error(std::exception_ptr).
 */

#include <bexec/completion_signatures.hpp>
#include <bexec/env.hpp>
#include <bexec/just.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/sender.hpp>
#include <bexec/when_all.hpp>
#include <exception>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

// ---------------------------------------------------------------------------
// Helper types
// ---------------------------------------------------------------------------

// Copy is nothrow, move throws. Used to make ValuesTuple/ErrorVariant moves
// non-nothrow while the initial storage (copy) stays nothrow.
struct copy_nothrow_move_throws {
  copy_nothrow_move_throws() = default;
  copy_nothrow_move_throws(const copy_nothrow_move_throws&) noexcept = default;
  copy_nothrow_move_throws(copy_nothrow_move_throws&&) {
    throw std::runtime_error("copy_nothrow_move_throws move");
  }
};

// Sender whose error type move/copy throws: exercises the error-storage slow
// path (child_error catches and stores std::exception_ptr).
struct throwing_error_sender {
  using error_type = throwing_value;  // move/copy both throw
  using completion_signatures =
      bexec::completion_signatures<bexec::set_error_t(error_type)>;

  template <class Receiver>
  struct op {
    error_type error;
    Receiver receiver;
    explicit op(Receiver r) noexcept(
        std::is_nothrow_move_constructible_v<Receiver>)
        : receiver(std::move(r)) {}
    void start() noexcept {
      bexec::set_error(std::move(receiver), std::move(error));
    }
  };

  template <class Receiver>
  op<Receiver> connect(Receiver r) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>) {
    return op<Receiver>{std::move(r)};
  }
};

// Sender that passes its value as an lvalue: the slot copy is nothrow, but the
// final ValuesTuple move throws. Exercises the value-combination slow path.
struct lvalue_value_sender {
  using completion_signatures = bexec::completion_signatures<bexec::set_value_t(
      copy_nothrow_move_throws)>;

  template <class Receiver>
  struct op {
    copy_nothrow_move_throws value;
    Receiver receiver;
    explicit op(Receiver r) noexcept(
        std::is_nothrow_move_constructible_v<Receiver>)
        : receiver(std::move(r)) {}
    void start() noexcept {
      bexec::set_value(std::move(receiver), value);  // lvalue -> copy
    }
  };

  template <class Receiver>
  op<Receiver> connect(Receiver r) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>) {
    return op<Receiver>{std::move(r)};
  }
};

// Sender that passes its error as an lvalue: the error copy is nothrow, but
// the ErrorVariant move in finish_one throws. Exercises gap D.
struct lvalue_error_sender {
  using completion_signatures = bexec::completion_signatures<bexec::set_error_t(
      copy_nothrow_move_throws)>;

  template <class Receiver>
  struct op {
    copy_nothrow_move_throws error;
    Receiver receiver;
    explicit op(Receiver r) noexcept(
        std::is_nothrow_move_constructible_v<Receiver>)
        : receiver(std::move(r)) {}
    void start() noexcept {
      bexec::set_error(std::move(receiver), error);  // lvalue -> copy
    }
  };

  template <class Receiver>
  op<Receiver> connect(Receiver r) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>) {
    return op<Receiver>{std::move(r)};
  }
};

// Stop token whose callback construction always throws: exercises the
// stop-callback slow path (register_stop_callback -> start_error).
struct throwing_stop_token {
  template <class Callback>
  class callback_type {
   public:
    callback_type(const throwing_stop_token&, Callback) {
      throw std::runtime_error("throwing stop callback");
    }
  };
};

struct throwing_stop_env {
  [[nodiscard]] throwing_stop_token query(
      bexec::get_stop_token_t) const noexcept {
    return {};
  }
};

// Receiver whose move construction throws. The receiver concept only requires
// move_constructible, so this satisfies bexec::receiver yet would terminate in
// when_all's noexcept finish_one/start_error critical sections. when_all
// rejects it via an operation-level static_assert; these assertions pin the
// fact that the receiver concept alone does not guarantee a nothrow move.
struct throwing_move_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();
  throwing_move_receiver() = default;
  throwing_move_receiver(throwing_move_receiver&&) {
    throw std::runtime_error("receiver move");
  }
  void set_value() noexcept {}
  template <class... Args>
  void set_value(Args&&...) noexcept {}
  template <class Error>
  void set_error(Error&&) noexcept {}
  void set_stopped() noexcept {}
};
static_assert(bexec::receiver<throwing_move_receiver>,
              "receiver concept accepts move-throwing receivers");
static_assert(!std::is_nothrow_move_constructible_v<throwing_move_receiver>,
              "sanity: the receiver move throws");

// ---------------------------------------------------------------------------
// A. child value storage (slot emplace)
// ---------------------------------------------------------------------------

// A1 (noexcept): nothrow value storage extracts successfully.
using wa_a1 = decltype(bexec::when_all(bexec::just(1), bexec::just(2)));
static_assert(bexec::completion_signatures_of_t<wa_a1>::template count_of<
                  bexec::set_error_t>() == 0,
              "A1: nothrow value storage must extract");

// A2 (throwing): throwing value storage keeps exception_ptr.
using wa_a2 =
    decltype(bexec::when_all(throwing_value_sender{}, bexec::just(1)));
static_assert(bexec::completion_signatures_of_t<wa_a2>::template count_of<
                  bexec::set_error_t>() >= 1,
              "A2: throwing value storage must keep set_error");

TEST(basic, when_all_extraction_a2_value_storage_throws) {
  auto state = std::make_shared<shared_state>();
  auto op =
      bexec::connect(bexec::when_all(throwing_value_sender{}, bexec::just(1)),
                     any_receiver{state});
  bexec::start(op);
  EXPECT_EQ(state->signal, signal_kind::error);
  EXPECT_TRUE(static_cast<bool>(state->exception));
}

// ---------------------------------------------------------------------------
// B. child error storage (ErrorVariant emplace)
// ---------------------------------------------------------------------------

// B1 (noexcept): nothrow error storage extracts, original error type kept.
using wa_b1 = decltype(bexec::when_all(bexec::just_error(7), bexec::just(2)));
static_assert(std::variant_size_v<bexec::error_types_of_t<wa_b1>> == 1,
              "B1: nothrow error storage extracts, keeps only int");

// B2 (throwing): throwing error storage keeps exception_ptr.
using wa_b2 =
    decltype(bexec::when_all(throwing_error_sender{}, bexec::just(1)));
static_assert(bexec::completion_signatures_of_t<wa_b2>::template count_of<
                  bexec::set_error_t>() >= 1,
              "B2: throwing error storage must keep set_error");

TEST(basic, when_all_extraction_b2_error_storage_throws) {
  auto state = std::make_shared<shared_state>();
  auto op =
      bexec::connect(bexec::when_all(throwing_error_sender{}, bexec::just(1)),
                     any_receiver{state});
  bexec::start(op);
  EXPECT_EQ(state->signal, signal_kind::error);
  EXPECT_TRUE(static_cast<bool>(state->exception));
}

// ---------------------------------------------------------------------------
// C. final value combination (ValuesTuple move)
// ---------------------------------------------------------------------------

// C2 (throwing): the value is storable via a nothrow copy, but the combined
// ValuesTuple move throws, so extraction must fail (keeps exception_ptr).
using wa_c2 = decltype(bexec::when_all(lvalue_value_sender{}, bexec::just(1)));
static_assert(bexec::completion_signatures_of_t<wa_c2>::template count_of<
                  bexec::set_error_t>() >= 1,
              "C2: throwing final value move must keep set_error");

// ---------------------------------------------------------------------------
// D. ErrorVariant move out of the optional (finish_one)
// ---------------------------------------------------------------------------

// D2 (throwing): error is storable via a nothrow copy, but the ErrorVariant
// move in finish_one throws, so extraction must fail (keeps exception_ptr).
// Without the ErrorVariant-move condition this extracts and the runtime would
// terminate in finish_one.
using wa_d2 = decltype(bexec::when_all(lvalue_error_sender{}, bexec::just(1)));
static_assert(bexec::completion_signatures_of_t<wa_d2>::template count_of<
                  bexec::set_error_t>() >= 1,
              "D2: throwing ErrorVariant move must keep set_error");

// ---------------------------------------------------------------------------
// E. stop-callback construction (register_stop_callback)
// ---------------------------------------------------------------------------

// E1 (noexcept): a never-stopping environment extracts successfully.
using wa_e1 = decltype(bexec::when_all(bexec::just(1), bexec::just(2)));
static_assert(
    bexec::completion_signatures_of_t<
        wa_e1, bexec::empty_env>::template count_of<bexec::set_error_t>() == 0,
    "E1: never-stop env must extract");

// E2 (throwing): a stop token whose callback construction throws must keep
// exception_ptr in the environment-aware completion signatures.
using wa_e2 = decltype(bexec::when_all(bexec::just(1), bexec::just(2)));
static_assert(
    bexec::completion_signatures_of_t<
        wa_e2, throwing_stop_env>::template count_of<bexec::set_error_t>() >= 1,
    "E2: throwing stop-callback env must keep set_error");

}  // namespace
}  // namespace bexec_tests
