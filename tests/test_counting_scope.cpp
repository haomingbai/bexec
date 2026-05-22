/**
 * @file tests/test_counting_scope.cpp
 * @brief Tests standard-style counting scopes and detached spawn.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/counting_scope.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sender.hpp>
#include <memory>
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
    CHECK(loop.run_one() == 0);
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
    CHECK(!scope.get_token().try_associate());
    CHECK(state->signal == signal_kind::none);

    association = {};
    CHECK(state->signal == signal_kind::none);
    CHECK(loop.run_one() == 1);
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
    CHECK(loop.run_one() == 0);
    second_association = {};

    CHECK(loop.run_one() == 1);
    CHECK(loop.run_one() == 1);
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
    CHECK(loop.run_one() == 0);
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
    CHECK(scope.request_stop());

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
    CHECK(loop.run_one() == 1);
    CHECK(state->signal == signal_kind::value);
  }

  {
    bexec::counting_scope scope;
    scope.close();
    bool started = false;
    bexec::spawn(flag_sender{started}, scope.get_token());
    CHECK(!started);
  }
}

}  // namespace bexec_tests
