/**
 * @file tests/basic/counting_scope.cpp
 * @brief Tests standard-style counting scopes and detached spawn.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/associate.hpp>
#include <bexec/completion_signatures.hpp>
#include <bexec/counting_scope.hpp>
#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/run_loop.hpp>
#include <bexec/sender.hpp>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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
using associated_just_sender_type = decltype(bexec::associate(
    bexec::just(42), std::declval<counting_scope_token_type>()));

static_assert(bexec::sender<join_sender_type>);
static_assert(!bexec::sender_to<join_sender_type, any_receiver>);
static_assert(bexec::scope_token<simple_scope_token_type>);
static_assert(bexec::scope_token<counting_scope_token_type>);
static_assert(bexec::scope_association<simple_scope_association_type>);
static_assert(bexec::scope_association<counting_scope_association_type>);
static_assert(bexec::sender<associated_just_sender_type>);
static_assert(bexec::sends_stopped<associated_just_sender_type>);
static_assert(std::move_constructible<associated_just_sender_type>);
static_assert(std::copy_constructible<associated_just_sender_type>);

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

struct stoppable_future_receiver : future_receiver {
  bexec::inplace_stop_token stop_token;

  [[nodiscard]] auto get_env() const noexcept {
    return bexec::env_with_stop_token{stop_token};
  }
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

struct concurrent_completion_state {
  std::mutex mutex;
  std::condition_variable condition;
  bool start_entered{false};
  bool allow_start_return{false};
  bool start_returned{false};
  bool completion_returned{false};
  bool child_destroyed{false};
  bool child_destroyed_before_start_return{false};
  void* operation{nullptr};
  void (*complete)(void*) noexcept {nullptr};
};

class concurrent_completion_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t()>;

  explicit concurrent_completion_sender(
      concurrent_completion_state& state) noexcept
      : state_(&state) {}

  template <class Receiver>
  class operation {
   public:
    operation(concurrent_completion_state& state, Receiver receiver)
        : state_(&state), receiver_(std::move(receiver)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    ~operation() {
      std::lock_guard lock(state_->mutex);
      state_->child_destroyed = true;
      state_->child_destroyed_before_start_return = !state_->start_returned;
      state_->condition.notify_all();
    }

    void start() noexcept {
      std::unique_lock lock(state_->mutex);
      state_->operation = this;
      state_->complete = [](void* raw) noexcept {
        static_cast<operation*>(raw)->complete();
      };
      state_->start_entered = true;
      state_->condition.notify_all();
      state_->condition.wait(lock,
                             [this] { return state_->allow_start_return; });
      state_->start_returned = true;
      state_->condition.notify_all();
    }

   private:
    void complete() noexcept {
      bexec::set_value(std::move(receiver_));

      std::lock_guard lock(state_->mutex);
      state_->completion_returned = true;
      state_->condition.notify_all();
    }

    concurrent_completion_state* state_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{*state_, std::move(receiver)};
  }

 private:
  concurrent_completion_state* state_;
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

struct connect_tracking_state {
  bool connected{false};
  bool started{false};
};

class connect_tracking_sender {
 public:
  using completion_signatures =
      bexec::completion_signatures<bexec::set_value_t()>;

  explicit connect_tracking_sender(connect_tracking_state& state) noexcept
      : state_(&state) {}

  template <class Receiver>
  class operation {
   public:
    operation(connect_tracking_state& state, Receiver receiver)
        : state_(&state), receiver_(std::move(receiver)) {
      state_->connected = true;
    }

    void start() noexcept {
      state_->started = true;
      bexec::set_value(std::move(receiver_));
    }

   private:
    connect_tracking_state* state_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) const {
    return operation<Receiver>{*state_, std::move(receiver)};
  }

 private:
  connect_tracking_state* state_;
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

TEST(basic, counting_scope_lifecycle_paths) {
  {
    bexec::simple_counting_scope scope;
    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;

    auto operation = connect_join(scope, std::move(receiver));
    auto token = scope.get_token();
    auto association = token.try_associate();
    EXPECT_TRUE(association);
    association = {};
    EXPECT_EQ(state->signal, signal_kind::none);

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::value);
    EXPECT_FALSE(scope.get_token().try_associate());
  }

  {
    bexec::simple_counting_scope scope;
    bexec::run_loop loop;
    auto token = scope.get_token();
    auto association = token.try_associate();
    EXPECT_TRUE(association);

    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    auto late_association = scope.get_token().try_associate();
    EXPECT_TRUE(late_association);
    EXPECT_EQ(state->signal, signal_kind::none);

    association = {};
    EXPECT_EQ(state->signal, signal_kind::none);
    late_association = {};
    EXPECT_EQ(state->signal, signal_kind::none);
    loop.finish();
    loop.run();
    EXPECT_EQ(state->signal, signal_kind::value);
    EXPECT_FALSE(scope.get_token().try_associate());
  }

  {
    bexec::simple_counting_scope scope;
    bexec::run_loop loop;
    auto token = scope.get_token();
    auto first_association = token.try_associate();
    auto second_association = token.try_associate();
    EXPECT_TRUE(first_association);
    EXPECT_TRUE(second_association);

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
    EXPECT_EQ(first_state->signal, signal_kind::value);
    EXPECT_EQ(second_state->signal, signal_kind::value);
  }

  {
    bexec::simple_counting_scope scope;
    scope.close();
    EXPECT_FALSE(scope.get_token().try_associate());

    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::value);
  }

  {
    bexec::simple_counting_scope scope;
    auto token = scope.get_token();
    auto association = token.try_associate();
    EXPECT_TRUE(association);
    scope.close();
    EXPECT_FALSE(scope.get_token().try_associate());
    association = {};

    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::value);
  }

  {
    bexec::counting_scope scope;
    stop_observer_state child_state;
    scope.request_stop();

    bexec::spawn(stop_observing_sender{child_state}, scope.get_token());
    EXPECT_FALSE(child_state.value);
    EXPECT_TRUE(child_state.stopped);

    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::value);
  }

  {
    bexec::counting_scope scope;
    lifetime_state child_state;
    bexec::spawn(lifetime_sender{child_state}, scope.get_token());
    EXPECT_TRUE(child_state.started);
    EXPECT_FALSE(child_state.destroyed);

    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::none);
    child_state.complete(child_state.operation);
    EXPECT_TRUE(child_state.destroyed);
    EXPECT_EQ(state->signal, signal_kind::none);
    loop.finish();
    loop.run();
    EXPECT_EQ(state->signal, signal_kind::value);
  }

  {
    bexec::counting_scope scope;
    scope.close();
    bool started = false;
    bexec::spawn(flag_sender{started}, scope.get_token());
    EXPECT_FALSE(started);
  }

  {
    bexec::counting_scope scope;
    auto future = bexec::spawn_future(bexec::just(42), scope.get_token());

    future_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(std::move(future), std::move(receiver));

    EXPECT_EQ(state->signal, signal_kind::none);
    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::value);
    EXPECT_EQ(state->first, 42);
  }

  {
    bexec::counting_scope scope;
    auto future = bexec::spawn_future(bexec::just(7, std::string{"ready"}),
                                      scope.get_token());

    future_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(std::move(future), std::move(receiver));

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::value);
    EXPECT_EQ(state->first, 7);
    EXPECT_EQ(state->second, "ready");
  }

  {
    bexec::counting_scope scope;
    auto future = bexec::spawn_future(bexec::just_error(std::string{"failed"}),
                                      scope.get_token());

    future_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(std::move(future), std::move(receiver));

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::error);
    EXPECT_EQ(state->error, "failed");
  }

  {
    bexec::counting_scope scope;
    auto future = bexec::spawn_future(bexec::just_stopped(), scope.get_token());

    future_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(std::move(future), std::move(receiver));

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::stopped);
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
    EXPECT_FALSE(started);
    EXPECT_EQ(state->signal, signal_kind::stopped);
  }

  {
    bexec::counting_scope scope;
    lifetime_state child_state;
    auto future =
        bexec::spawn_future(lifetime_sender{child_state}, scope.get_token());
    EXPECT_TRUE(child_state.started);
    EXPECT_FALSE(child_state.destroyed);

    bexec::run_loop loop;
    env_receiver<scheduler_env> join_receiver;
    join_receiver.env = scheduler_env{loop.get_scheduler()};
    auto join_state = join_receiver.state;
    auto join_operation = connect_join(scope, std::move(join_receiver));
    bexec::start(join_operation);
    EXPECT_EQ(join_state->signal, signal_kind::none);

    child_state.complete(child_state.operation);
    EXPECT_FALSE(child_state.destroyed);
    EXPECT_EQ(join_state->signal, signal_kind::none);

    {
      future_receiver receiver;
      auto state = receiver.state;
      auto future_operation =
          bexec::connect(std::move(future), std::move(receiver));
      bexec::start(future_operation);
      EXPECT_EQ(state->signal, signal_kind::value);
      EXPECT_EQ(join_state->signal, signal_kind::none);
    }

    EXPECT_TRUE(child_state.destroyed);
    loop.finish();
    loop.run();
    EXPECT_EQ(join_state->signal, signal_kind::value);
  }

  {
    bexec::counting_scope scope;
    async_stop_state child_state;

    {
      auto future = bexec::spawn_future(async_stop_sender{child_state},
                                        scope.get_token());
      EXPECT_TRUE(child_state.started);
      EXPECT_FALSE(child_state.stop_requested);
    }

    child_state.check_stop(child_state.operation);
    EXPECT_TRUE(child_state.stop_requested);
    EXPECT_FALSE(child_state.destroyed);
    child_state.complete(child_state.operation);
    EXPECT_TRUE(child_state.destroyed);

    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto operation = connect_join(scope, std::move(receiver));

    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::value);
  }
}

TEST(basic, counting_scope_spawn_waits_for_start_before_releasing_state) {
  bexec::simple_counting_scope scope;
  concurrent_completion_state child_state;

  std::thread starter([&] {
    bexec::spawn(concurrent_completion_sender{child_state}, scope.get_token());
  });

  void* child_operation = nullptr;
  void (*complete)(void*) noexcept = nullptr;
  {
    std::unique_lock lock(child_state.mutex);
    child_state.condition.wait(lock, [&] { return child_state.start_entered; });
    child_operation = child_state.operation;
    complete = child_state.complete;
  }

  std::thread completer([&] { complete(child_operation); });

  {
    std::unique_lock lock(child_state.mutex);
    child_state.condition.wait(lock,
                               [&] { return child_state.completion_returned; });
    EXPECT_FALSE(child_state.child_destroyed);
    child_state.allow_start_return = true;
    child_state.condition.notify_all();
  }

  completer.join();
  starter.join();

  {
    std::lock_guard lock(child_state.mutex);
    EXPECT_TRUE(child_state.start_returned);
    EXPECT_TRUE(child_state.child_destroyed);
    EXPECT_FALSE(child_state.child_destroyed_before_start_return);
  }

  scope.close();
  bexec::run_loop loop;
  env_receiver<scheduler_env> receiver;
  receiver.env = scheduler_env{loop.get_scheduler()};
  auto state = receiver.state;
  auto join_operation = connect_join(scope, std::move(receiver));
  bexec::start(join_operation);
  EXPECT_EQ(state->signal, signal_kind::value);
}

TEST(basic, counting_scope_associate_marks_sender_and_reports_rejection) {
  {
    bexec::simple_counting_scope scope;
    {
      bool started = false;
      auto associated =
          bexec::associate(flag_sender{started}, scope.get_token());

      future_receiver receiver;
      auto state = receiver.state;
      auto operation =
          bexec::connect(std::move(associated), std::move(receiver));

      EXPECT_FALSE(started);
      bexec::start(operation);
      EXPECT_TRUE(started);
      EXPECT_EQ(state->signal, signal_kind::value);
    }

    scope.close();
    bexec::run_loop loop;
    env_receiver<scheduler_env> receiver;
    receiver.env = scheduler_env{loop.get_scheduler()};
    auto state = receiver.state;
    auto join_operation = connect_join(scope, std::move(receiver));
    bexec::start(join_operation);
    EXPECT_EQ(state->signal, signal_kind::value);
  }

  {
    bexec::simple_counting_scope scope;
    scope.close();
    connect_tracking_state child_state;
    auto associated = connect_tracking_sender{child_state} |
                      bexec::associate(scope.get_token());

    future_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(std::move(associated), std::move(receiver));

    bexec::start(operation);
    EXPECT_FALSE(child_state.connected);
    EXPECT_FALSE(child_state.started);
    EXPECT_EQ(state->signal, signal_kind::stopped);
  }
}

TEST(basic, counting_scope_spawn_connects_before_rejected_association) {
  {
    bexec::simple_counting_scope scope;
    scope.close();
    connect_tracking_state child_state;

    bexec::spawn(connect_tracking_sender{child_state}, scope.get_token());

    EXPECT_TRUE(child_state.connected);
    EXPECT_FALSE(child_state.started);
  }

  {
    bexec::simple_counting_scope scope;
    scope.close();
    connect_tracking_state child_state;
    auto future = bexec::spawn_future(connect_tracking_sender{child_state},
                                      scope.get_token());

    EXPECT_TRUE(child_state.connected);
    EXPECT_FALSE(child_state.started);

    future_receiver receiver;
    auto state = receiver.state;
    auto operation = bexec::connect(std::move(future), std::move(receiver));
    bexec::start(operation);
    EXPECT_EQ(state->signal, signal_kind::stopped);
  }
}

TEST(basic, counting_scope_spawn_future_forwards_downstream_stop) {
  bexec::counting_scope scope;
  async_stop_state child_state;
  auto future =
      bexec::spawn_future(async_stop_sender{child_state}, scope.get_token());
  ASSERT_TRUE(child_state.started);

  bexec::inplace_stop_source stop_source;
  stoppable_future_receiver receiver;
  receiver.stop_token = stop_source.get_token();
  auto state = receiver.state;
  auto operation = bexec::connect(std::move(future), std::move(receiver));
  bexec::start(operation);

  stop_source.request_stop();
  EXPECT_EQ(state->signal, signal_kind::stopped);

  child_state.check_stop(child_state.operation);
  EXPECT_TRUE(child_state.stop_requested);
  child_state.complete(child_state.operation);
  EXPECT_TRUE(child_state.destroyed);

  scope.close();
  bexec::run_loop loop;
  env_receiver<scheduler_env> join_receiver;
  join_receiver.env = scheduler_env{loop.get_scheduler()};
  auto join_state = join_receiver.state;
  auto join_operation = connect_join(scope, std::move(join_receiver));
  bexec::start(join_operation);
  EXPECT_EQ(join_state->signal, signal_kind::value);
}

TEST(basic, counting_scope_destruction_enforces_lifecycle) {
  EXPECT_DEATH(
      {
        std::optional<bexec::simple_counting_scope::association> association;
        {
          bexec::simple_counting_scope scope;
          association.emplace(scope.get_token().try_associate());
        }
      },
      "");

  EXPECT_DEATH(
      {
        std::optional<bexec::counting_scope::association> association;
        {
          bexec::counting_scope scope;
          association.emplace(scope.get_token().try_associate());
        }
      },
      "");
}

}  // namespace bexec_tests
