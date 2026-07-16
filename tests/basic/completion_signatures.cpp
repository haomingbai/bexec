/**
 * @file tests/basic/completion_signatures.cpp
 * @brief Tests completion-signature metadata helpers.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Verifies valid signature packs, value/error/stopped introspection, and type
 * aggregation behavior used by senders and adaptors.
 */

#include <bexec/completion_signatures.hpp>
#include <bexec/env.hpp>
#include <bexec/just.hpp>
#include <bexec/let.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>
#include <exception>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

struct good_receiver {
  void set_value() noexcept {}
  void set_value(int) noexcept {}
  void set_error(std::exception_ptr) noexcept {}
  void set_stopped() noexcept {}
};

struct throwing_receiver {
  void set_value() {}
  void set_error(std::exception_ptr) {}
  void set_stopped() {}
};

struct good_operation {
  void start() noexcept {}
};

struct throwing_operation {
  void start() {}
};

template <class Operation>
concept rvalue_startable =
    requires(Operation operation) { bexec::start(std::move(operation)); };

template <class Receiver>
concept lvalue_set_value_receiver =
    requires(Receiver receiver) { bexec::set_value(receiver); };

struct get_flag_t {
  template <class Env>
  constexpr auto operator()(Env&& env) const noexcept
      -> decltype(std::as_const(env).query(*this)) {
    return std::as_const(env).query(*this);
  }
};

inline constexpr get_flag_t get_flag{};

struct flag_env {
  [[nodiscard]] int query(get_flag_t) const noexcept { return 1; }
};

struct env_dependent_sender {
  template <class Self>
  [[nodiscard]] static consteval auto get_completion_signatures() {
    return bexec::completion_signatures<bexec::set_value_t()>{};
  }

  template <class Self, class Env>
  [[nodiscard]] static consteval auto get_completion_signatures() {
    if constexpr (requires { bexec::query(std::declval<Env>(), get_flag); }) {
      return bexec::completion_signatures<bexec::set_value_t(int)>{};
    } else {
      return bexec::completion_signatures<bexec::set_value_t()>{};
    }
  }
};

}  // namespace

TEST(basic, completion_signature_contracts) {
  using just_int = decltype(bexec::just(1));
  static_assert(!bexec::sender<int>);
  static_assert(
      std::same_as<bexec::completion_signatures_of_t<just_int>,
                   bexec::completion_signatures<bexec::set_value_t(int)>>);
  static_assert(std::same_as<bexec::value_types_of_t<just_int>,
                             std::variant<std::tuple<int>>>);
  static_assert(
      std::same_as<bexec::error_types_of_t<just_int>, bexec::type_list<>>);
  static_assert(!bexec::sends_stopped<just_int>);

  using just_error_string = decltype(bexec::just_error(std::string{}));
  static_assert(std::same_as<
                bexec::completion_signatures_of_t<just_error_string>,
                bexec::completion_signatures<bexec::set_error_t(std::string)>>);
  static_assert(std::same_as<bexec::error_types_of_t<just_error_string>,
                             std::variant<std::string>>);

  using just_stopped = decltype(bexec::just_stopped());
  static_assert(
      std::same_as<bexec::completion_signatures_of_t<just_stopped>,
                   bexec::completion_signatures<bexec::set_stopped_t()>>);
  static_assert(bexec::sends_stopped<just_stopped>);

  using then_int = decltype(bexec::just(1) |
                            bexec::then([](int value) { return value + 1; }));
  static_assert(std::same_as<bexec::value_types_of_t<then_int>,
                             std::variant<std::tuple<int>>>);
  static_assert(std::same_as<bexec::error_types_of_t<then_int>,
                             std::variant<std::exception_ptr>>);

  using let_value_string = decltype(bexec::just(1) | bexec::let_value([](int) {
                                      return bexec::just(std::string{});
                                    }));
  static_assert(std::same_as<bexec::value_types_of_t<let_value_string>,
                             std::variant<std::tuple<std::string>>>);
  static_assert(std::same_as<bexec::error_types_of_t<let_value_string>,
                             std::variant<std::exception_ptr>>);

  using let_error_int =
      decltype(bexec::just_error(std::string{}) |
               bexec::let_error([](std::string) { return bexec::just(1); }));
  static_assert(std::same_as<bexec::value_types_of_t<let_error_int>,
                             std::variant<std::tuple<int>>>);
  static_assert(std::same_as<bexec::error_types_of_t<let_error_int>,
                             std::variant<std::exception_ptr>>);

  using let_stopped_int =
      decltype(bexec::just_stopped() |
               bexec::let_stopped([] { return bexec::just(1); }));
  static_assert(std::same_as<bexec::value_types_of_t<let_stopped_int>,
                             std::variant<std::tuple<int>>>);
  static_assert(!bexec::sends_stopped<let_stopped_int>);

  using all = decltype(bexec::when_all(bexec::just(), bexec::just_error(7)));
  static_assert(std::same_as<bexec::completion_signatures_of_t<all>,
                             bexec::completion_signatures<
                                 bexec::set_error_t(int),
                                 bexec::set_error_t(std::exception_ptr)>>);
  static_assert(std::same_as<bexec::error_types_of_t<all>,
                             std::variant<int, std::exception_ptr>>);

  static_assert(bexec::sender<env_dependent_sender>);
  static_assert(bexec::sender_in<env_dependent_sender, flag_env>);
  static_assert(
      std::same_as<bexec::completion_signatures_of_t<env_dependent_sender>,
                   bexec::completion_signatures<bexec::set_value_t()>>);
  static_assert(
      std::same_as<
          bexec::completion_signatures_of_t<env_dependent_sender, flag_env>,
          bexec::completion_signatures<bexec::set_value_t(int)>>);

  using env_all = decltype(bexec::when_all(env_dependent_sender{}));
  static_assert(
      std::same_as<
          bexec::completion_signatures_of_t<env_all>,
          bexec::completion_signatures<
              bexec::set_value_t(), bexec::set_error_t(std::exception_ptr)>>);
  static_assert(
      std::same_as<bexec::completion_signatures_of_t<env_all, flag_env>,
                   bexec::completion_signatures<bexec::set_value_t(int),
                                                bexec::set_error_t(
                                                    std::exception_ptr)>>);

  static_assert(bexec::operation_state<good_operation>);
  static_assert(!bexec::operation_state<throwing_operation>);
  static_assert(!rvalue_startable<good_operation>);

  using receiver_completions =
      bexec::completion_signatures<bexec::set_value_t(),
                                   bexec::set_error_t(std::exception_ptr),
                                   bexec::set_stopped_t()>;
  static_assert(bexec::receiver_of<good_receiver, receiver_completions>);
  static_assert(!bexec::receiver_of<throwing_receiver, receiver_completions>);
  static_assert(!lvalue_set_value_receiver<good_receiver>);

  bexec::inplace_stop_source source;
  bexec::env_with_stop_token env{source.get_token()};
  auto token_from_object = bexec::get_stop_token(env);
  auto token_from_query = bexec::query(env, bexec::get_stop_token);

  EXPECT_TRUE(!token_from_object.stop_requested());
  EXPECT_TRUE(!token_from_query.stop_requested());
  source.request_stop();
  EXPECT_TRUE(token_from_object.stop_requested());
  EXPECT_TRUE(token_from_query.stop_requested());
}

}  // namespace bexec_tests
