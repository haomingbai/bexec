/**
 * @file tests/test_counting_scope.cpp
 * @brief Tests standard-style counting scopes and detached spawn.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/completion_signatures.hpp>
#include <bexec/counting_scope.hpp>
#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sender.hpp>
#include <memory>
#include <string>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {
namespace {

struct scheduler_env {
  bexec::run_loop::scheduler scheduler;

  [[nodiscard]] bexec::run_loop::scheduler query(
      bexec::get_scheduler_t) const noexcept {
    return scheduler;
  }

  [[nodiscard]] bexec::run_loop::scheduler query(
      bexec::get_delegation_scheduler_t) const noexcept {
    return scheduler;
  }
};

using join_sender_type =
    decltype(std::declval<bexec::simple_counting_scope&>().join());
using simple_scope_token_type =
    decltype(std::declval<bexec::simple_counting_scope&>().get_token());
using simple_scope_association_type =
    decltype(std::declval<const simple_scope_token_type&>().try_associate());
using counting_scope_token_type =
    decltype(std::declval<bexec::counting_scope&>().get_token());
using counting_scope_association_type =
    decltype(std::declval<const counting_scope_token_type&>().try_associate());

static_assert(bexec::sender<join_sender_type>);
static_assert(!bexec::sender_to<join_sender_type, any_receiver>);
static_assert(bexec::scope_token<simple_scope_token_type>);
static_assert(bexec::scope_token<counting_scope_token_type>);
static_assert(bexec::scope_association<simple_scope_association_type>);
static_assert(bexec::scope_association<counting_scope_association_type>);

struct stop_observer_state {
  bool value{false};
  bool stopped{false};
};

struct future_receiver_state {
  bexec_tests::signal_kind signal{bexec_tests::signal_kind::none};
  int first{0};
  std::string second;
  std::string error;
};

struct future_receiver {
  std::shared_ptr<future_receiver_state> state =
      std::make_shared<future_receiver_state>();

  void set_value() noexcept { state->signal = signal_kind::value; }

  void set_value(int value) noexcept {
    state->signal = signal_kind::value;
    state->first = value;
  }

  void set_value(int first, std::string second) noexcept {
    state->signal = signal_kind::value;
    state->first = first;
    state->second = std::move(second);
  }

  void set_error(std::string error) noexcept {
    state->signal = signal_kind::error;
    state->error = std::move(error);
  }

  template <class Error>
  void set_error(Error&&) noexcept {
    state->signal = signal_kind::error;
  }

  void set_stopped() noexcept { state->signal = signal_kind::stopped; }
};

class stop_observing_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(),
                                   bexec::set_stopped_t()>;

  explicit stop_observing_sender(stop_observer_state& state) noexcept
      : state_(&state) {}

  template <class Receiver>
  class operation {
   public:
    operation(stop_observer_state& state, Receiver receiver)
        : state_(&state), receiver_(std::move(receiver)) {}

    void start() noexcept {
      auto token = bexec::get_stop_token(bexec::get_env(receiver_));
      if (token.stop_requested()) {
        state_->stopped = true;
        bexec::set_stopped(std::move(receiver_));
      } else {
        state_->value = true;
        bexec::set_value(std::move(receiver_));
      }
    }

   private:
    stop_observer_state* state_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{*state_, std::move(receiver)};
  }

 private:
  stop_observer_state* state_;
};

struct lifetime_state {
  bool started{false};
  bool destroyed{false};
  void* operation{nullptr};
  void (*complete)(void*) noexcept {nullptr};
};

class lifetime_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t()>;

  explicit lifetime_sender(lifetime_state& state) noexcept : state_(&state) {}

  template <class Receiver>
  class operation {
   public:
    operation(lifetime_state& state, Receiver receiver)
        : state_(&state), receiver_(std::move(receiver)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    ~operation() { state_->destroyed = true; }

    void start() noexcept {
      state_->started = true;
      state_->operation = this;
      state_->complete = [](void* raw) noexcept {
        static_cast<operation*>(raw)->complete();
      };
    }

   private:
    void complete() noexcept { bexec::set_value(std::move(receiver_)); }

    lifetime_state* state_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{*state_, std::move(receiver)};
  }

 private:
  lifetime_state* state_;
};

class flag_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t()>;

  explicit flag_sender(bool& started) noexcept : started_(&started) {}

  template <class Receiver>
  class operation {
   public:
    operation(bool& started, Receiver receiver)
        : started_(&started), receiver_(std::move(receiver)) {}

    void start() noexcept {
      *started_ = true;
      bexec::set_value(std::move(receiver_));
    }

   private:
    bool* started_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{*started_, std::move(receiver)};
  }

 private:
  bool* started_;
};

struct async_stop_state {
  bool started{false};
  bool stop_requested{false};
  bool destroyed{false};
  void* operation{nullptr};
  void (*complete)(void*) noexcept {nullptr};
  void (*check_stop)(void*) noexcept {nullptr};
};

class async_stop_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t(),
                                   bexec::set_stopped_t()>;

  explicit async_stop_sender(async_stop_state& state) noexcept
      : state_(&state) {}

  template <class Receiver>
  class operation {
   public:
    using stop_token_type = decltype(bexec::get_stop_token(
        bexec::get_env(std::declval<Receiver&>())));

    operation(async_stop_state& state, Receiver receiver)
        : state_(&state),
          receiver_(std::move(receiver)),
          stop_token_(bexec::get_stop_token(bexec::get_env(receiver_))) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    ~operation() { state_->destroyed = true; }

    void start() noexcept {
      state_->started = true;
      state_->operation = this;
      state_->complete = [](void* raw) noexcept {
        static_cast<operation*>(raw)->complete();
      };
      state_->check_stop = [](void* raw) noexcept {
        static_cast<operation*>(raw)->check_stop();
      };
    }

   private:
    void check_stop() noexcept {
      state_->stop_requested = stop_token_.stop_requested();
    }

    void complete() noexcept {
      check_stop();
      if (state_->stop_requested) {
        bexec::set_stopped(std::move(receiver_));
      } else {
        bexec::set_value(std::move(receiver_));
      }
    }

    async_stop_state* state_;
    Receiver receiver_;
    stop_token_type stop_token_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{*state_, std::move(receiver)};
  }

 private:
  async_stop_state* state_;
};

template <class Receiver>
auto connect_join(bexec::simple_counting_scope& scope, Receiver receiver) {
  return bexec::connect(scope.join(), std::move(receiver));
}

template <class Receiver>
auto connect_join(bexec::counting_scope& scope, Receiver receiver) {
  return bexec::connect(scope.join(), std::move(receiver));
}

}  // namespace

void test_counting_scope() {
  {
    bexec::simple_counting_scope scope;
    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;

    auto operation = connect_join(scope, std::move(receiver));
    auto token = scope.get_token();
    auto association = token.try_associate();
    CHECK(association);
    association = {};
    CHECK(state->signal == signal_kind::none);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(!scope.get_token().try_associate());
  }

  {
    bexec::simple_counting_scope scope;
    bexec::run_loop loop;
    auto token = scope.get_token();
    auto association = token.try_associate();
    CHECK(association);

    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    auto late_association = scope.get_token().try_associate();
    CHECK(late_association);
    CHECK(state->signal == signal_kind::none);

    association = {};
    CHECK(state->signal == signal_kind::none);
    late_association = {};
    CHECK(state->signal == signal_kind::none);
    loop.finish();
    loop.run();
    CHECK(state->signal == signal_kind::value);
    CHECK(!scope.get_token().try_associate());
  }

  {
    bexec::simple_counting_scope scope;
    bexec::run_loop loop;
    auto token = scope.get_token();
    auto first_association = token.try_associate();
    auto second_association = token.try_associate();
    CHECK(first_association);
    CHECK(second_association);

    env_receiver<scheduler_env> first_receiver;
    first_receiver.env = scheduler_env{loop.get_scheduler()};
    auto first_state = first_receiver.state;
    auto first = connect_join(scope, std::move(first_receiver));

    env_receiver<scheduler_env> second_receiver;
    second_receiver.env = scheduler_env{loop.get_scheduler()};
    auto second_state = second_receiver.state;
    auto second = connect_join(scope, std::move(second_receiver));

    bexec::start(first);
    bexec::start(second);
    first_association = {};
    second_association = {};

    loop.finish();
    loop.run();
    CHECK(first_state->signal == signal_kind::value);
    CHECK(second_state->signal == signal_kind::value);
  }

  {
    bexec::simple_counting_scope scope;
    scope.close();
    CHECK(!scope.get_token().try_associate());

    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
  }

  {
    bexec::simple_counting_scope scope;
    auto token = scope.get_token();
    auto association = token.try_associate();
    CHECK(association);
    scope.close();
    CHECK(!scope.get_token().try_associate());
    association = {};

    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
  }

  {
    bexec::counting_scope scope;
    stop_observer_state child_state;
    scope.request_stop();

    bexec::spawn(stop_observing_sender{child_state}, scope.get_token());
    CHECK(!child_state.value);
    CHECK(child_state.stopped);

    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
  }

  {
    bexec::counting_scope scope;
    lifetime_state child_state;
    bexec::spawn(lifetime_sender{child_state}, scope.get_token());
    CHECK(child_state.started);
    CHECK(!child_state.destroyed);

    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    CHECK(state->signal == signal_kind::none);
    child_state.complete(child_state.operation);
    CHECK(child_state.destroyed);
    CHECK(state->signal == signal_kind::none);
    loop.finish();
    loop.run();
    CHECK(state->signal == signal_kind::value);
  }

  {
    bexec::counting_scope scope;
    scope.close();
    bool started = false;
    bexec::spawn(flag_sender{started}, scope.get_token());
    CHECK(!started);
  }

  {
    bexec::counting_scope scope;
    auto future = bexec::spawn_future(bexec::just(42), scope.get_token());

    future_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(std::move(future), std::move(receiver));

    CHECK(state->signal == signal_kind::none);
    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->first == 42);
  }

  {
    bexec::counting_scope scope;
    auto future = bexec::spawn_future(bexec::just(7, std::string{"ready"}),
                                      scope.get_token());

    future_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(std::move(future), std::move(receiver));

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->first == 7);
    CHECK(state->second == "ready");
  }

  {
    bexec::counting_scope scope;
    auto future = bexec::spawn_future(bexec::just_error(std::string{"failed"}),
                                      scope.get_token());

    future_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(std::move(future), std::move(receiver));

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
    CHECK(state->error == "failed");
  }

  {
    bexec::counting_scope scope;
    auto future = bexec::spawn_future(bexec::just_stopped(), scope.get_token());

    future_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(std::move(future), std::move(receiver));

    bexec::start(operation);
    CHECK(state->signal == signal_kind::stopped);
  }

  {
    bexec::counting_scope scope;
    scope.close();
    bool started = false;
    auto future = bexec::spawn_future(flag_sender{started}, scope.get_token());

    future_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(std::move(future), std::move(receiver));

    bexec::start(operation);
    CHECK(!started);
    CHECK(state->signal == signal_kind::stopped);
  }

  {
    bexec::counting_scope scope;
    lifetime_state child_state;
    auto future =
        bexec::spawn_future(lifetime_sender{child_state}, scope.get_token());
    CHECK(child_state.started);
    CHECK(!child_state.destroyed);

    bexec::run_loop loop;
    env_receiver<scheduler_env> join_receiver;
    join_receiver.env = scheduler_env{loop.get_scheduler()};
    auto join_state = join_receiver.state;
    auto join_operation = connect_join(scope, std::move(join_receiver));
    bexec::start(join_operation);
    CHECK(join_state->signal == signal_kind::none);

    child_state.complete(child_state.operation);
    CHECK(!child_state.destroyed);
    CHECK(join_state->signal == signal_kind::none);

    {
      future_receiver receiver;
      auto state = receiver.state;
      auto future_operation =
          bexec::connect(std::move(future), std::move(receiver));
      bexec::start(future_operation);
      CHECK(state->signal == signal_kind::value);
      CHECK(join_state->signal == signal_kind::none);
    }

    CHECK(child_state.destroyed);
    loop.finish();
    loop.run();
    CHECK(join_state->signal == signal_kind::value);
  }

  {
    bexec::counting_scope scope;
    async_stop_state child_state;

    {
      auto future = bexec::spawn_future(async_stop_sender{child_state},
                                        scope.get_token());
      CHECK(child_state.started);
      CHECK(!child_state.stop_requested);
    }

    child_state.check_stop(child_state.operation);
    CHECK(child_state.stop_requested);
    CHECK(!child_state.destroyed);
    child_state.complete(child_state.operation);
    CHECK(child_state.destroyed);

    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
  }
}

}  // namespace bexec_tests
