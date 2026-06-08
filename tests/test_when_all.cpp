/**
 * @file tests/test_when_all.cpp
 * @brief Tests the when_all sender algorithm.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Verifies all-success completion, error variant aggregation, first-error
 * selection, stopped propagation, and scheduler-based child completion.
 */

#include <bexec/env.hpp>
#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

struct multi_value_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();

  void set_value(int first, int second, std::string text) noexcept {
    state->signal = signal_kind::value;
    state->int_value = first + second;
    state->string_value = std::move(text);
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    state->signal = signal_kind::error;
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }
};

struct move_only_when_all_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();

  void set_value(std::unique_ptr<int> value, int extra) noexcept {
    state->signal = signal_kind::value;
    state->int_value = *value + extra;
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    state->signal = signal_kind::error;
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }
};

struct pair_value_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();

  void set_value(int first, int second) noexcept {
    state->signal = signal_kind::value;
    state->int_value = first + second;
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    state->signal = signal_kind::error;
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }
};

struct raw_error_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();

  template <class... Args>
  void set_value(Args&&...) noexcept {
    state->signal = signal_kind::value;
  }

  void set_error(int value) noexcept {
    state->signal = signal_kind::error;
    state->int_value = value;
  }

  void set_error(std::exception_ptr error) noexcept {
    state->signal = signal_kind::error;
    state->exception = error;
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }
};

struct stop_token_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();
  bexec::env_with_stop_token<> env;

  explicit stop_token_receiver(bexec::inplace_stop_token token) : env(token) {}

  void set_value() noexcept { state->signal = signal_kind::value; }

  template <class Error>
  void set_error(Error&&) noexcept {
    state->signal = signal_kind::error;
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }

  [[nodiscard]] bexec::env_with_stop_token<> get_env() const noexcept {
    return env;
  }
};

struct large_stop_token {
  template <class Callback>
  class callback_type {
   public:
    callback_type(const large_stop_token&, Callback callback) noexcept
        : callback_(std::move(callback)) {}

    callback_type(const callback_type&) = delete;
    callback_type& operator=(const callback_type&) = delete;
    callback_type(callback_type&&) = delete;
    callback_type& operator=(callback_type&&) = delete;

   private:
    Callback callback_;
    std::byte padding_[320]{};
  };

  [[nodiscard]] bool stop_requested() const noexcept { return false; }
};

struct large_stop_env {
  [[nodiscard]] large_stop_token query(bexec::get_stop_token_t) const noexcept {
    return {};
  }
};

struct large_stop_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();

  void set_value() noexcept { state->signal = signal_kind::value; }

  template <class Error>
  void set_error(Error&&) noexcept {
    state->signal = signal_kind::error;
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }

  [[nodiscard]] large_stop_env get_env() const noexcept { return {}; }
};

struct noop_callback {
  void operator()() const noexcept {}
};

static_assert(sizeof(large_stop_token::callback_type<noop_callback>) > 256U);

class non_movable_value_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(int)>;

  explicit non_movable_value_sender(int value) : value_(value) {}

  template <class Receiver>
  class operation {
   public:
    operation(int value, Receiver receiver)
        : value_(value), receiver_(std::move(receiver)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    void start() noexcept { bexec::set_value(std::move(receiver_), value_); }

   private:
    int value_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return operation<Receiver>{value_, std::move(receiver)};
  }

 private:
  int value_;
};

class choice_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(int),
                                   bexec::set_value_t(std::string)>;

  explicit choice_sender(bool use_string) : use_string_(use_string) {}

  template <class Receiver>
  class operation {
   public:
    operation(bool use_string, Receiver receiver)
        : use_string_(use_string), receiver_(std::move(receiver)) {}

    void start() noexcept {
      if (use_string_) {
        bexec::set_value(std::move(receiver_), std::string{"selected"});
      } else {
        bexec::set_value(std::move(receiver_), 17);
      }
    }

   private:
    bool use_string_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{use_string_, std::move(receiver)};
  }

 private:
  bool use_string_;
};

template <class Variant>
struct one_variant_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();
  std::shared_ptr<std::optional<Variant>> value =
      std::make_shared<std::optional<Variant>>();

  void set_value(Variant variant) noexcept {
    state->signal = signal_kind::value;
    *value = std::move(variant);
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    state->signal = signal_kind::error;
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }
};

struct two_variant_receiver {
  std::shared_ptr<shared_state> state = std::make_shared<shared_state>();

  template <class First, class Second>
  void set_value(First first, Second second) noexcept {
    state->signal = signal_kind::value;
    if (std::holds_alternative<std::tuple<int>>(first) &&
        std::holds_alternative<std::tuple<std::string>>(second)) {
      state->int_value = std::get<0>(std::get<std::tuple<int>>(first));
      state->string_value =
          std::get<0>(std::get<std::tuple<std::string>>(second));
    }
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    state->signal = signal_kind::error;
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }
};

}  // namespace

void test_when_all() {
  static_assert(!std::is_invocable_v<decltype(bexec::when_all)>);

  {
    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(
        bexec::when_all(bexec::just(), bexec::just()), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
  }

  {
    bool transformed = false;
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::when_all(bexec::just(), bexec::just()) |
                  bexec::then([&] { transformed = true; });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(transformed);
    CHECK(state->signal == signal_kind::value);
  }

  {
    auto receiver = multi_value_receiver{};
    auto state = receiver.state;
    auto operation = bexec::connect(
        bexec::when_all(bexec::just(1, 2), bexec::just(std::string{"ok"})),
        receiver);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 3);
    CHECK(state->string_value == "ok");
  }

  {
    auto receiver = move_only_when_all_receiver{};
    auto state = receiver.state;
    auto operation = bexec::connect(
        bexec::when_all(bexec::just(std::make_unique<int>(37)), bexec::just(5)),
        receiver);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 42);
  }

  {
    auto receiver = pair_value_receiver{};
    auto state = receiver.state;
    auto operation = bexec::connect(
        bexec::when_all(non_movable_value_sender{9}, bexec::just(1)), receiver);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 10);
  }

  {
    using sender_type =
        decltype(bexec::when_all(bexec::just(), bexec::just_error(7)));
    using variant_type = sender_type::error_variant;
    static_assert(std::variant_size_v<variant_type> >= 2);

    variant_receiver<variant_type> receiver;
    auto state = receiver.state;
    auto error = receiver.error;
    auto operation = bexec::connect(
        bexec::when_all(bexec::just(), bexec::just_error(7)), receiver);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
    CHECK(error->has_value());
    CHECK(std::holds_alternative<int>(**error));
    CHECK(std::get<int>(**error) == 7);
  }

  {
    auto receiver = raw_error_receiver{};
    auto state = receiver.state;
    auto operation = bexec::connect(
        bexec::when_all(bexec::just(), bexec::just_error(9)), receiver);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
    CHECK(state->int_value == 9);
  }

  {
    using sender_type = decltype(bexec::when_all(
        bexec::just_error(3), bexec::just_error(std::string{"later"})));
    using variant_type = sender_type::error_variant;
    static_assert(std::variant_size_v<variant_type> >= 3);

    variant_receiver<variant_type> receiver;
    auto state = receiver.state;
    auto error = receiver.error;
    auto operation =
        bexec::connect(bexec::when_all(bexec::just_error(3),
                                       bexec::just_error(std::string{"later"})),
                       receiver);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
    CHECK(error->has_value());
    CHECK(std::holds_alternative<int>(**error));
    CHECK(std::get<int>(**error) == 3);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto operation =
        bexec::connect(bexec::when_all(bexec::just(), bexec::just_stopped()),
                       any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::stopped);
  }

  {
    bexec::run_loop loop;
    int count = 0;

    auto first =
        bexec::schedule(loop.get_scheduler()) | bexec::then([&] { ++count; });
    auto second =
        bexec::schedule(loop.get_scheduler()) | bexec::then([&] { ++count; });
    auto state = std::make_shared<shared_state>();
    auto operation =
        bexec::connect(bexec::when_all(std::move(first), std::move(second)),
                       any_receiver{state});

    bexec::start(operation);
    loop.finish();
    loop.run();
    CHECK(count == 2);
    CHECK(state->signal == signal_kind::value);
  }

  {
    bexec::run_loop loop;
    bexec::inplace_stop_source source;

    stop_token_receiver receiver{source.get_token()};
    auto state = receiver.state;

    auto operation =
        bexec::connect(bexec::when_all(bexec::schedule(loop.get_scheduler()),
                                       bexec::schedule(loop.get_scheduler())),
                       std::move(receiver));

    bexec::start(operation);
    source.request_stop();

    loop.finish();
    loop.run();
    CHECK(state->signal == signal_kind::stopped);
  }

  {
    large_stop_receiver receiver;
    auto state = receiver.state;
    auto operation =
        bexec::connect(bexec::when_all(bexec::just()), std::move(receiver));

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
  }

  {
    using variant_sender = decltype(bexec::into_variant(choice_sender{false}));
    using variant_type = variant_sender::value_variant;
    static_assert(
        std::same_as<variant_type,
                     std::variant<std::tuple<int>, std::tuple<std::string>>>);

    one_variant_receiver<variant_type> receiver;
    auto state = receiver.state;
    auto value = receiver.value;
    auto operation =
        bexec::connect(bexec::into_variant(choice_sender{false}), receiver);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(value->has_value());
    CHECK(std::holds_alternative<std::tuple<int>>(**value));
    CHECK(std::get<0>(std::get<std::tuple<int>>(**value)) == 17);
  }

  {
    two_variant_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(
        bexec::when_all_with_variant(choice_sender{false}, choice_sender{true}),
        receiver);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 17);
    CHECK(state->string_value == "selected");
  }
}

}  // namespace bexec_tests
