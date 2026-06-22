/**
 * @file include/bexec/detail/counting_scope.hpp
 * @brief Internal helpers for counting scopes and spawn algorithms.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-28
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines implementation-only stop-token wrappers, receivers, operation states,
 * and result storage used by public counting_scope, spawn, and spawn_future.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_COUNTING_SCOPE_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_COUNTING_SCOPE_HPP_

#include <atomic>
#include <bexec/completion_signatures.hpp>
#include <bexec/detail/config.hpp>
#include <bexec/detail/manual_lifetime.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/env.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <bexec/stop_token.hpp>
#include <concepts>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace bexec {

namespace detail {

template <class BaseToken>
class scope_stop_token;

template <class Scheduler>
using scope_schedule_sender_for_t =
    remove_cvref_t<decltype(bexec::schedule(std::declval<Scheduler&>()))>;

template <class Receiver>
using scope_receiver_scheduler_t = remove_cvref_t<decltype(bexec::get_scheduler(
    bexec::get_env(std::declval<Receiver&>())))>;

struct scope_join_waiter {
  scope_join_waiter* next{nullptr};
  virtual void complete_deferred() noexcept = 0;

 protected:
  ~scope_join_waiter() = default;
};

template <class BaseToken, class Callback>
class scope_stop_callback {
 public:
  scope_stop_callback(const scope_stop_callback&) = delete;
  scope_stop_callback& operator=(const scope_stop_callback&) = delete;
  scope_stop_callback(scope_stop_callback&&) = delete;
  scope_stop_callback& operator=(scope_stop_callback&&) = delete;

  template <class CallbackArg>
  scope_stop_callback(inplace_stop_token scope_token, BaseToken base_token,
                      CallbackArg&& callback)
      : callback_(std::forward<CallbackArg>(callback)) {
    scope_callback_.emplace(scope_token, callback_ref{this});
    base_callback_.emplace(base_token, callback_ref{this});
  }

  template <class CallbackArg>
  scope_stop_callback(const scope_stop_token<BaseToken>& token,
                      CallbackArg&& callback);

 private:
  struct callback_ref {
    scope_stop_callback* self;

    void operator()() const noexcept { self->call_once(); }
  };

  void call_once() noexcept {
    bool expected = false;
    if (!called_.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
      return;
    }

    callback_();
  }

  using scope_callback_type =
      typename inplace_stop_token::template callback_type<callback_ref>;
  using base_callback_type =
      typename BaseToken::template callback_type<callback_ref>;

  std::decay_t<Callback> callback_;
  std::atomic_bool called_{false};
  std::optional<scope_callback_type> scope_callback_;
  std::optional<base_callback_type> base_callback_;
};

template <class BaseToken>
class scope_stop_token {
 public:
  template <class Callback>
  using callback_type = scope_stop_callback<BaseToken, std::decay_t<Callback>>;

  scope_stop_token(inplace_stop_token scope_token, BaseToken base_token)
      : scope_token_(std::move(scope_token)),
        base_token_(std::move(base_token)) {}

  [[nodiscard]] bool stop_requested() const noexcept {
    return scope_token_.stop_requested() || base_token_.stop_requested();
  }

 private:
  template <class, class>
  friend class scope_stop_callback;

  inplace_stop_token scope_token_;
  BaseToken base_token_;
};

template <class BaseToken, class Callback>
template <class CallbackArg>
scope_stop_callback<BaseToken, Callback>::scope_stop_callback(
    const scope_stop_token<BaseToken>& token, CallbackArg&& callback)
    : scope_stop_callback(token.scope_token_, token.base_token_,
                          std::forward<CallbackArg>(callback)) {}

template <class BaseEnv>
class env_with_scope_stop_token {
 public:
  env_with_scope_stop_token(inplace_stop_token scope_token, BaseEnv base)
      : scope_token_(std::move(scope_token)), base_(std::move(base)) {}

  [[nodiscard]] auto query(get_stop_token_t) const noexcept {
    return scope_stop_token{scope_token_, bexec::get_stop_token(base_)};
  }

  template <class QueryTag, class... Args>
    requires(!std::same_as<std::remove_cvref_t<QueryTag>, get_stop_token_t> &&
             requires(const BaseEnv& base, QueryTag&& tag, Args&&... args) {
               bexec::query(base, std::forward<QueryTag>(tag),
                            std::forward<Args>(args)...);
             })
  decltype(auto) query(QueryTag&& tag, Args&&... args) const
      noexcept(noexcept(bexec::query(base_, std::forward<QueryTag>(tag),
                                     std::forward<Args>(args)...))) {
    return bexec::query(base_, std::forward<QueryTag>(tag),
                        std::forward<Args>(args)...);
  }

 private:
  inplace_stop_token scope_token_;
  BaseEnv base_;
};

template <class Receiver>
class scope_stop_receiver {
 public:
  scope_stop_receiver(Receiver receiver, inplace_stop_token scope_token)
      : receiver_(std::move(receiver)), scope_token_(std::move(scope_token)) {}

  [[nodiscard]] auto get_env() const
      noexcept(noexcept(bexec::get_env(receiver_))) {
    return env_with_scope_stop_token{scope_token_, bexec::get_env(receiver_)};
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
    bexec::set_value(std::move(receiver_), std::forward<Args>(args)...);
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    bexec::set_error(std::move(receiver_), std::forward<Error>(error));
  }

  void set_stopped() noexcept { bexec::set_stopped(std::move(receiver_)); }

 private:
  Receiver receiver_;
  inplace_stop_token scope_token_;
};

template <class Sender>
class scope_stop_sender {
 public:
  using completion_signatures = bexec::completion_signatures_of_t<Sender>;

  template <class Self, class Env>
  [[nodiscard]] static consteval auto get_completion_signatures() {
    return bexec::completion_signatures_of_t<Sender, Env>{};
  }

  scope_stop_sender(Sender sender, inplace_stop_token scope_token)
      : sender_(std::move(sender)), scope_token_(std::move(scope_token)) {}

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return bexec::connect(
        std::move(sender_),
        scope_stop_receiver<Receiver>{std::move(receiver), scope_token_});
  }

  template <class Receiver>
    requires std::copy_constructible<Sender>
  auto connect(Receiver receiver) const& {
    return bexec::connect(sender_, scope_stop_receiver<Receiver>{
                                       std::move(receiver), scope_token_});
  }

 private:
  Sender sender_;
  inplace_stop_token scope_token_;
};

template <class Signature>
struct spawn_completion_signature_ok : std::false_type {};

template <>
struct spawn_completion_signature_ok<set_value_t()> : std::true_type {};

template <>
struct spawn_completion_signature_ok<set_stopped_t()> : std::true_type {};

template <class Completions>
struct spawn_completion_signatures_ok;

template <class... Signatures>
struct spawn_completion_signatures_ok<completion_signatures<Signatures...>>
    : std::bool_constant<(spawn_completion_signature_ok<Signatures>::value &&
                          ...)> {};

template <class Sender>
inline constexpr bool spawnable_sender_v =
    sender<Sender> && spawn_completion_signatures_ok<
                          bexec::completion_signatures_of_t<Sender>>::value;

template <class Token, class Sender>
auto wrap_scope_sender(const Token& token, Sender&& sender) noexcept(
    noexcept(token.wrap(std::forward<Sender>(sender))))
    -> decltype(token.wrap(std::forward<Sender>(sender))) {
  return token.wrap(std::forward<Sender>(sender));
}

template <class Operation>
class spawn_receiver {
 public:
  explicit spawn_receiver(Operation& operation) : operation_(&operation) {}

  [[nodiscard]] auto get_env() const noexcept(noexcept(operation_->env())) {
    return operation_->env();
  }

  void set_value() noexcept { operation_->complete(); }
  void set_stopped() noexcept { operation_->complete(); }

 private:
  Operation* operation_;
};

template <class Sender, class Association, class Env, class ByteAllocator>
class spawn_operation {
 public:
  using operation_type = spawn_operation;
  using receiver_type = spawn_receiver<operation_type>;
  using child_operation_type = decltype(bexec::connect(
      std::declval<Sender>(), std::declval<receiver_type>()));
  using byte_allocator_type = ByteAllocator;
  using allocator_type = typename std::allocator_traits<
      byte_allocator_type>::template rebind_alloc<operation_type>;
  using allocator_traits = std::allocator_traits<allocator_type>;

  spawn_operation(Sender sender, Association association, Env env,
                  allocator_type allocator)
      : sender_(std::move(sender)),
        association_(std::move(association)),
        env_(std::move(env)),
        allocator_(std::move(allocator)) {}

  spawn_operation(const spawn_operation&) = delete;
  spawn_operation& operator=(const spawn_operation&) = delete;
  spawn_operation(spawn_operation&&) = delete;
  spawn_operation& operator=(spawn_operation&&) = delete;

  void start() noexcept {
    bool complete_now = false;
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      child_.emplace_from([this]() -> child_operation_type {
        return bexec::connect(std::move(sender_), receiver_type{*this});
      });
      {
        std::lock_guard lock(mutex_);
        starting_ = true;
      }
      bexec::start(*child_);
      {
        std::lock_guard lock(mutex_);
        starting_ = false;
        complete_now = completed_;
      }
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      {
        std::lock_guard lock(mutex_);
        starting_ = false;
      }
      complete_now = true;
    }
#endif

    if (complete_now) {
      finish();
    }
  }

  [[nodiscard]] Env env() const
      noexcept(std::is_nothrow_copy_constructible_v<Env>) {
    return env_;
  }

  void complete() noexcept {
    bool complete_now = false;
    {
      std::lock_guard lock(mutex_);
      if (starting_) {
        completed_ = true;
      } else {
        complete_now = true;
      }
    }

    if (complete_now) {
      finish();
    }
  }

 private:
  void finish() noexcept {
    Association association = std::move(association_);
    allocator_type allocator = allocator_;
    spawn_operation* self = this;
    child_.reset();
    allocator_traits::destroy(allocator, self);
    allocator_traits::deallocate(allocator, self, 1);
  }

  Sender sender_;
  Association association_;
  Env env_;
  allocator_type allocator_;
  detail::manual_lifetime<child_operation_type> child_;
  std::mutex mutex_;
  bool starting_{false};
  bool completed_{false};
};

template <class Completions>
struct spawn_future_all_nothrow_decay;

template <class... Signatures>
struct spawn_future_all_nothrow_decay<completion_signatures<Signatures...>>
    : std::bool_constant<(completion_signature_nothrow_decay_v<Signatures> &&
                          ...)> {};

template <class Completions>
inline constexpr bool spawn_future_all_nothrow_decay_v =
    spawn_future_all_nothrow_decay<Completions>::value;

template <class Completions>
struct spawn_future_result_variant;

template <class... Signatures>
struct spawn_future_result_variant<completion_signatures<Signatures...>> {
  using exception_result = std::tuple<set_error_t, std::exception_ptr>;
  using result_list = unique_type_list_t<concat_type_lists_t<
      type_list<std::monostate, std::tuple<set_stopped_t>>,
      maybe_type_list_t<!spawn_future_all_nothrow_decay_v<
                            completion_signatures<Signatures...>>,
                        exception_result>,
      type_list<completion_result_tuple_t<Signatures>...>>>;
  using type = variant_from_type_list_t<result_list>;
};

template <class Completions>
using spawn_future_result_variant_t =
    typename spawn_future_result_variant<Completions>::type;

template <class Completions>
struct spawn_future_completion_signatures;

template <class... Signatures>
struct spawn_future_completion_signatures<
    completion_signatures<Signatures...>> {
  using exception_signature = set_error_t(std::exception_ptr);
  using signature_list = unique_type_list_t<concat_type_lists_t<
      type_list<set_stopped_t()>,
      maybe_type_list_t<!spawn_future_all_nothrow_decay_v<
                            completion_signatures<Signatures...>>,
                        exception_signature>,
      type_list<decayed_completion_signature_t<Signatures>...>>>;
  using type = completion_signatures_from_type_list_t<signature_list>;
};

template <class Completions>
using spawn_future_completion_signatures_t =
    typename spawn_future_completion_signatures<Completions>::type;

template <class State, class Env>
class spawn_future_receiver {
 public:
  spawn_future_receiver(State* state, inplace_stop_token stop_token,
                        const Env& env) noexcept
      : state_(state), stop_token_(std::move(stop_token)), env_(&env) {}

  [[nodiscard]] auto get_env() const noexcept {
    return env_with_scope_stop_token<const Env&>{stop_token_, *env_};
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
    set_complete<set_value_t>(std::forward<Args>(args)...);
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    set_complete<set_error_t>(std::forward<Error>(error));
  }

  void set_stopped() noexcept { set_complete<set_stopped_t>(); }

 private:
  template <class Tag, class... Args>
  void set_complete(Args&&... args) noexcept {
    using result_type = std::tuple<Tag, std::decay_t<Args>...>;
    constexpr bool nothrow =
        (std::is_nothrow_constructible_v<std::decay_t<Args>, Args> && ...);

#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    if constexpr (!nothrow) {
      try {
        state_->result().template emplace<result_type>(
            Tag{}, std::forward<Args>(args)...);
      } catch (...) {
        using error_result = std::tuple<set_error_t, std::exception_ptr>;
        state_->result().template emplace<error_result>(
            set_error_t{}, std::current_exception());
      }
    } else
#endif
    {
      state_->result().template emplace<result_type>(
          Tag{}, std::forward<Args>(args)...);
    }
#if !BEXEC_DETAIL_EXCEPTIONS_ENABLED
    (void)nothrow;
#endif

    state_->complete();
  }

  State* state_;
  inplace_stop_token stop_token_;
  const Env* env_;
};

template <class Sender, class Token, class Env, class ByteAllocator>
class spawn_future_state {
 public:
  using child_env = env_with_scope_stop_token<const Env&>;
  using completions = bexec::completion_signatures_of_t<Sender, child_env>;
  using completion_signatures =
      spawn_future_completion_signatures_t<completions>;
  using result_variant = spawn_future_result_variant_t<completions>;
  using receiver_type = spawn_future_receiver<spawn_future_state, Env>;
  using operation_type = decltype(bexec::connect(
      std::declval<Sender>(), std::declval<receiver_type>()));
  using byte_allocator_type = ByteAllocator;
  using allocator_type = typename std::allocator_traits<
      byte_allocator_type>::template rebind_alloc<spawn_future_state>;
  using allocator_traits = std::allocator_traits<allocator_type>;
  using association_type =
      remove_cvref_t<decltype(std::declval<Token&>().try_associate())>;

  spawn_future_state(byte_allocator_type byte_allocator, Sender&& sender,
                     Token token, Env env)
      : byte_allocator_(std::move(byte_allocator)),
        env_(std::move(env)),
        operation_(bexec::connect(
            std::move(sender),
            receiver_type{this, stop_source_.get_token(), env_})) {
    association_ = token.try_associate();
    if (association_) {
      bexec::start(operation_);
    } else {
      bexec::set_stopped(receiver_type{this, stop_source_.get_token(), env_});
    }
  }

  spawn_future_state(const spawn_future_state&) = delete;
  spawn_future_state& operator=(const spawn_future_state&) = delete;
  spawn_future_state(spawn_future_state&&) = delete;
  spawn_future_state& operator=(spawn_future_state&&) = delete;

  [[nodiscard]] result_variant& result() noexcept { return result_; }

  void complete() noexcept {
    void* receiver = nullptr;
    void (*deliver)(spawn_future_state*, void*) noexcept = nullptr;
    bool destroy = false;
    {
      std::lock_guard lock(mutex_);
      completed_ = true;
      if (consumer_ != nullptr) {
        receiver = consumer_;
        deliver = deliver_;
      } else if (abandoned_) {
        destroy = true;
      }
    }

    if (deliver != nullptr) {
      deliver(this, receiver);
    } else if (destroy) {
      destroy_self();
    }
  }

  template <class Receiver>
  void consume(Receiver& receiver) noexcept {
    bool deliver_now = false;
    {
      std::lock_guard lock(mutex_);
      if (completed_) {
        deliver_now = true;
      } else {
        consumer_ = &receiver;
        deliver_ = deliver_to_receiver<Receiver>;
      }
    }

    if (deliver_now) {
      deliver_to_receiver<Receiver>(this, &receiver);
    }
  }

  void abandon() noexcept {
    bool destroy = false;
    bool request_stop = false;
    {
      std::lock_guard lock(mutex_);
      if (completed_) {
        destroy = true;
      } else {
        abandoned_ = true;
        request_stop = true;
      }
    }

    if (request_stop) {
      stop_source_.request_stop();
    }
    if (destroy) {
      destroy_self();
    }
  }

 private:
  template <class Receiver>
  static void deliver_to_receiver(spawn_future_state* state,
                                  void* raw_receiver) noexcept {
    Receiver& receiver = *static_cast<Receiver*>(raw_receiver);
    std::visit(
        [&receiver](auto& result) noexcept {
          using result_type = remove_cvref_t<decltype(result)>;
          if constexpr (!std::same_as<result_type, std::monostate>) {
            std::apply(
                [&receiver](auto& tag, auto&... values) noexcept {
                  tag(std::move(receiver), std::move(values)...);
                },
                result);
          }
        },
        state->result_);
  }

  void destroy_self() noexcept {
    association_type association = std::move(association_);
    allocator_type allocator{std::move(byte_allocator_)};
    spawn_future_state* self = this;
    allocator_traits::destroy(allocator, self);
    allocator_traits::deallocate(allocator, self, 1);
  }

  byte_allocator_type byte_allocator_;
  Env env_;
  inplace_stop_source stop_source_;
  operation_type operation_;
  association_type association_;
  result_variant result_;
  std::mutex mutex_;
  void* consumer_{nullptr};
  void (*deliver_)(spawn_future_state*, void*) noexcept {nullptr};
  bool completed_{false};
  bool abandoned_{false};
};

template <class State>
class spawn_future_state_handle {
 public:
  spawn_future_state_handle() noexcept = default;
  explicit spawn_future_state_handle(State* state) noexcept : state_(state) {}

  spawn_future_state_handle(const spawn_future_state_handle&) = delete;
  spawn_future_state_handle& operator=(const spawn_future_state_handle&) =
      delete;

  spawn_future_state_handle(spawn_future_state_handle&& other) noexcept
      : state_(std::exchange(other.state_, nullptr)) {}

  spawn_future_state_handle& operator=(
      spawn_future_state_handle&& other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
  }

  ~spawn_future_state_handle() { reset(); }

  [[nodiscard]] State* get() const noexcept { return state_; }

 private:
  void reset() noexcept {
    if (state_ == nullptr) {
      return;
    }

    std::exchange(state_, nullptr)->abandon();
  }

  State* state_{nullptr};
};

template <class State>
class spawn_future_sender {
 public:
  using completion_signatures = typename State::completion_signatures;

  explicit spawn_future_sender(spawn_future_state_handle<State> state) noexcept
      : state_(std::move(state)) {}

  spawn_future_sender(const spawn_future_sender&) = delete;
  spawn_future_sender& operator=(const spawn_future_sender&) = delete;
  spawn_future_sender(spawn_future_sender&&) noexcept = default;
  spawn_future_sender& operator=(spawn_future_sender&&) noexcept = default;

  template <class Receiver>
  class operation {
   public:
    operation(spawn_future_state_handle<State> state, Receiver receiver)
        : state_(std::move(state)), receiver_(std::move(receiver)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    void start() noexcept { state_.get()->consume(receiver_); }

   private:
    spawn_future_state_handle<State> state_;
    Receiver receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return operation<Receiver>{std::move(state_), std::move(receiver)};
  }

 private:
  spawn_future_state_handle<State> state_;
};

}  // namespace detail

namespace detail {

struct scope_token_test_sender {
  using completion_signatures = bexec::completion_signatures<set_value_t()>;
};

}  // namespace detail

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_COUNTING_SCOPE_HPP_
