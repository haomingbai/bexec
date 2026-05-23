/**
 * @file include/bexec/counting_scope.hpp
 * @brief Standard-style counting scopes and detached spawn.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_COUNTING_SCOPE_HPP_
#define BEXEC_INCLUDE_BEXEC_COUNTING_SCOPE_HPP_

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

    try {
      callback_();
    } catch (...) {
      std::terminate();
    }
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

template <class Signature>
struct spawn_future_result_tuple;

template <class Tag, class... Args>
struct spawn_future_result_tuple<Tag(Args...)> {
  using type = std::tuple<Tag, std::decay_t<Args>...>;
};

template <class Signature>
using spawn_future_result_tuple_t =
    typename spawn_future_result_tuple<Signature>::type;

template <class Signature>
struct spawn_future_decayed_signature;

template <class Tag, class... Args>
struct spawn_future_decayed_signature<Tag(Args...)> {
  using type = Tag(std::decay_t<Args>...);
};

template <class Signature>
using spawn_future_decayed_signature_t =
    typename spawn_future_decayed_signature<Signature>::type;

template <class Signature>
struct spawn_future_signature_nothrow_decay;

template <class Tag, class... Args>
struct spawn_future_signature_nothrow_decay<Tag(Args...)>
    : std::bool_constant<(
          std::is_nothrow_constructible_v<std::decay_t<Args>, Args> && ...)> {};

template <bool Include, class... Ts>
struct spawn_future_maybe_type_list {
  using type = type_list<>;
};

template <class... Ts>
struct spawn_future_maybe_type_list<true, Ts...> {
  using type = type_list<Ts...>;
};

template <bool Include, class... Ts>
using spawn_future_maybe_type_list_t =
    typename spawn_future_maybe_type_list<Include, Ts...>::type;

template <class Completions>
struct spawn_future_all_nothrow_decay;

template <class... Signatures>
struct spawn_future_all_nothrow_decay<completion_signatures<Signatures...>>
    : std::bool_constant<(
          spawn_future_signature_nothrow_decay<Signatures>::value && ...)> {};

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
      spawn_future_maybe_type_list_t<!spawn_future_all_nothrow_decay_v<
                                         completion_signatures<Signatures...>>,
                                     exception_result>,
      type_list<spawn_future_result_tuple_t<Signatures>...>>>;
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
      spawn_future_maybe_type_list_t<!spawn_future_all_nothrow_decay_v<
                                         completion_signatures<Signatures...>>,
                                     exception_signature>,
      type_list<spawn_future_decayed_signature_t<Signatures>...>>>;
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
    try {
#endif
      state_->result().template emplace<result_type>(
          Tag{}, std::forward<Args>(args)...);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      if constexpr (!nothrow) {
        using error_result = std::tuple<set_error_t, std::exception_ptr>;
        state_->result().template emplace<error_result>(
            set_error_t{}, std::current_exception());
      } else {
        std::terminate();
      }
    }
#else
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

template <class Assoc>
concept scope_association =
    std::movable<detail::remove_cvref_t<Assoc>> &&
    std::is_nothrow_move_constructible_v<detail::remove_cvref_t<Assoc>> &&
    std::is_nothrow_move_assignable_v<detail::remove_cvref_t<Assoc>> &&
    std::is_nothrow_default_constructible_v<detail::remove_cvref_t<Assoc>> &&
    std::default_initializable<detail::remove_cvref_t<Assoc>> &&
    requires(const detail::remove_cvref_t<Assoc>& assoc) {
      { static_cast<bool>(assoc) } noexcept;
      {
        assoc.try_associate()
      } noexcept -> std::same_as<detail::remove_cvref_t<Assoc>>;
    };

namespace detail {

struct scope_token_test_sender {
  using completion_signatures = bexec::completion_signatures<set_value_t()>;
};

}  // namespace detail

template <class Token>
concept scope_token =
    std::copyable<detail::remove_cvref_t<Token>> &&
    std::is_nothrow_copy_constructible_v<detail::remove_cvref_t<Token>> &&
    std::is_nothrow_move_constructible_v<detail::remove_cvref_t<Token>> &&
    std::is_nothrow_copy_assignable_v<detail::remove_cvref_t<Token>> &&
    std::is_nothrow_move_assignable_v<detail::remove_cvref_t<Token>> &&
    requires(const detail::remove_cvref_t<Token>& token,
             detail::scope_token_test_sender sender) {
      { token.try_associate() } noexcept -> scope_association;
      { token.wrap(std::move(sender)) } noexcept -> bexec::sender;
    };

class simple_counting_scope {
 public:
  class association;
  class token;
  class join_sender;

  simple_counting_scope() = default;
  simple_counting_scope(const simple_counting_scope&) = delete;
  simple_counting_scope& operator=(const simple_counting_scope&) = delete;
  ~simple_counting_scope() noexcept { ensure_destructible(); }

  [[nodiscard]] token get_token() noexcept;
  [[nodiscard]] join_sender join() noexcept;

  void close() noexcept {
    std::lock_guard lock(mutex_);
    switch (state_.load(std::memory_order_acquire)) {
      case state::unused:
        compare_exchange_state(state::unused, state::unused_and_closed);
        break;
      case state::open:
        compare_exchange_state(state::open, state::closed);
        break;
      case state::open_and_joining:
        compare_exchange_state(state::open_and_joining,
                               state::closed_and_joining);
        break;
      case state::closed:
      case state::unused_and_closed:
      case state::closed_and_joining:
      case state::joined:
        break;
    }
  }

 private:
  enum class state {
    unused,
    open,
    closed,
    unused_and_closed,
    open_and_joining,
    closed_and_joining,
    joined
  };

  friend class token;
  friend class join_sender;
  friend class counting_scope;

  [[nodiscard]] association try_associate() noexcept;

  void ensure_destructible() noexcept {
    std::lock_guard lock(mutex_);
    switch (state_.load(std::memory_order_acquire)) {
      case state::unused:
      case state::unused_and_closed:
      case state::joined:
        return;
      case state::open:
      case state::closed:
      case state::open_and_joining:
      case state::closed_and_joining:
        std::terminate();
    }
  }

  void disassociate() noexcept {
    detail::scope_join_waiter* completions = nullptr;
    {
      std::lock_guard lock(mutex_);
      if (decrement_count() != 0) {
        return;
      }

      switch (state_.load(std::memory_order_acquire)) {
        case state::open:
          compare_exchange_state(state::open, state::unused);
          break;
        case state::closed:
          compare_exchange_state(state::closed, state::unused_and_closed);
          break;
        case state::open_and_joining:
          compare_exchange_state(state::open_and_joining, state::joined);
          completions = joiners_;
          joiners_ = nullptr;
          break;
        case state::closed_and_joining:
          compare_exchange_state(state::closed_and_joining, state::joined);
          completions = joiners_;
          joiners_ = nullptr;
          break;
        case state::unused:
        case state::unused_and_closed:
        case state::joined:
          std::terminate();
      }
    }

    complete_all(completions);
  }

  bool start_join(detail::scope_join_waiter& waiter) noexcept {
    {
      std::lock_guard lock(mutex_);
      switch (state_.load(std::memory_order_acquire)) {
        case state::unused:
          compare_exchange_state(state::unused, state::joined);
          return true;
        case state::unused_and_closed:
          compare_exchange_state(state::unused_and_closed, state::joined);
          return true;
        case state::joined:
          return true;
        case state::open:
          compare_exchange_state(state::open, state::open_and_joining);
          push_joiner(waiter);
          return false;
        case state::closed:
          compare_exchange_state(state::closed, state::closed_and_joining);
          push_joiner(waiter);
          return false;
        case state::open_and_joining:
        case state::closed_and_joining:
          push_joiner(waiter);
          return false;
      }
    }

    return true;
  }

  void push_joiner(detail::scope_join_waiter& waiter) noexcept {
    waiter.next = joiners_;
    joiners_ = &waiter;
  }

  void compare_exchange_state(state expected, state desired) noexcept {
    if (!state_.compare_exchange_strong(expected, desired,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
      std::terminate();
    }
  }

  void compare_exchange_count(std::size_t expected,
                              std::size_t desired) noexcept {
    if (!count_.compare_exchange_strong(expected, desired,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
      std::terminate();
    }
  }

  void increment_count() noexcept {
    std::size_t current = count_.load(std::memory_order_acquire);
    for (;;) {
      if (current == static_cast<std::size_t>(-1)) {
        std::terminate();
      }
      if (count_.compare_exchange_weak(current, current + 1,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
        return;
      }
    }
  }

  std::size_t decrement_count() noexcept {
    std::size_t current = count_.load(std::memory_order_acquire);
    for (;;) {
      if (current == 0) {
        std::terminate();
      }
      if (count_.compare_exchange_weak(current, current - 1,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
        return current - 1;
      }
    }
  }

  static void complete_all(detail::scope_join_waiter* waiter) noexcept {
    while (waiter != nullptr) {
      detail::scope_join_waiter* next = waiter->next;
      waiter->next = nullptr;
      waiter->complete_deferred();
      waiter = next;
    }
  }

  std::mutex mutex_;
  std::atomic<state> state_{state::unused};
  std::atomic<std::size_t> count_{0};
  detail::scope_join_waiter* joiners_{nullptr};
};

class simple_counting_scope::association {
 public:
  association() noexcept = default;

  association(const association&) = delete;
  association& operator=(const association&) = delete;

  association(association&& other) noexcept
      : scope_(std::exchange(other.scope_, nullptr)) {}

  association& operator=(association&& other) noexcept {
    if (this != &other) {
      reset();
      scope_ = std::exchange(other.scope_, nullptr);
    }
    return *this;
  }

  ~association() { reset(); }

  [[nodiscard]] explicit operator bool() const noexcept {
    return scope_ != nullptr;
  }

  [[nodiscard]] association try_associate() const noexcept {
    if (scope_ == nullptr) {
      return {};
    }
    return scope_->try_associate();
  }

 private:
  friend class simple_counting_scope;

  explicit association(simple_counting_scope& scope) noexcept
      : scope_(&scope) {}

  void reset() noexcept {
    if (scope_ == nullptr) {
      return;
    }
    simple_counting_scope* scope = std::exchange(scope_, nullptr);
    scope->disassociate();
  }

  simple_counting_scope* scope_{nullptr};
};

inline simple_counting_scope::association
simple_counting_scope::try_associate() noexcept {
  std::lock_guard lock(mutex_);
  switch (state_.load(std::memory_order_acquire)) {
    case state::unused:
      compare_exchange_count(0, 1);
      compare_exchange_state(state::unused, state::open);
      return association{*this};
    case state::open:
    case state::open_and_joining:
      increment_count();
      return association{*this};
    case state::closed:
    case state::unused_and_closed:
    case state::closed_and_joining:
    case state::joined:
      return {};
  }

  return {};
}

class simple_counting_scope::token {
 public:
  token() = default;

  [[nodiscard]] association try_associate() const noexcept {
    if (scope_ == nullptr) {
      return {};
    }
    return scope_->try_associate();
  }

  template <bexec::sender Sender>
  [[nodiscard]] Sender&& wrap(Sender&& sender) const noexcept {
    return std::forward<Sender>(sender);
  }

  void disassociate() noexcept {
    if (scope_ == nullptr) {
      std::terminate();
    }
    scope_->disassociate();
  }

 private:
  friend class simple_counting_scope;

  explicit token(simple_counting_scope& scope) noexcept : scope_(&scope) {}

  simple_counting_scope* scope_{nullptr};
};

class simple_counting_scope::join_sender {
 public:
  using completion_signatures = bexec::completion_signatures<
      set_value_t(), set_error_t(std::exception_ptr), set_stopped_t()>;

  explicit join_sender(simple_counting_scope& scope) noexcept
      : scope_(&scope) {}

  template <class Receiver>
  class operation : private detail::scope_join_waiter {
   public:
    template <class Operation>
    class final_receiver {
     public:
      explicit final_receiver(Operation& operation) noexcept
          : operation_(&operation) {}

      [[nodiscard]] auto get_env() const
          noexcept(noexcept(bexec::get_env(operation_->receiver()))) {
        return bexec::get_env(operation_->receiver());
      }

      void set_value() noexcept {
        bexec::set_value(std::move(operation_->receiver_));
      }

      template <class Error>
      void set_error(Error&& error) noexcept {
        bexec::set_error(std::move(operation_->receiver_),
                         std::forward<Error>(error));
      }

      void set_stopped() noexcept {
        bexec::set_stopped(std::move(operation_->receiver_));
      }

     private:
      Operation* operation_;
    };

    using scheduler_type = detail::scope_receiver_scheduler_t<Receiver>;
    using schedule_sender_type =
        detail::scope_schedule_sender_for_t<scheduler_type>;
    using final_receiver_type = final_receiver<operation>;
    using final_operation_type =
        decltype(bexec::connect(std::declval<schedule_sender_type>(),
                                std::declval<final_receiver_type>()));

    operation(simple_counting_scope& scope, Receiver receiver)
        : scope_(&scope),
          scheduler_(bexec::get_scheduler(bexec::get_env(receiver))),
          receiver_(std::move(receiver)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    void start() noexcept {
      if (scope_->start_join(*this)) {
        bexec::set_value(std::move(receiver_));
      }
    }

    [[nodiscard]] Receiver& receiver() noexcept { return receiver_; }

   private:
    void complete_deferred() noexcept override {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      try {
#endif
        final_operation_.emplace_from([this]() -> final_operation_type {
          return bexec::connect(bexec::schedule(scheduler_),
                                final_receiver_type{*this});
        });
        bexec::start(*final_operation_);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      } catch (...) {
        bexec::set_error(std::move(receiver_), std::current_exception());
      }
#endif
    }

    simple_counting_scope* scope_;
    scheduler_type scheduler_;
    Receiver receiver_;
    detail::manual_lifetime<final_operation_type> final_operation_;
  };

  template <class Receiver>
    requires requires(Receiver& receiver) {
      bexec::get_scheduler(bexec::get_env(receiver));
    }
  auto connect(Receiver receiver) const {
    return operation<Receiver>{*scope_, std::move(receiver)};
  }

 private:
  simple_counting_scope* scope_;
};

inline simple_counting_scope::token
simple_counting_scope::get_token() noexcept {
  return token{*this};
}

inline simple_counting_scope::join_sender
simple_counting_scope::join() noexcept {
  return join_sender{*this};
}

class counting_scope {
 public:
  class association;
  class token;

  counting_scope() = default;
  counting_scope(const counting_scope&) = delete;
  counting_scope& operator=(const counting_scope&) = delete;
  ~counting_scope() noexcept { scope_.ensure_destructible(); }

  [[nodiscard]] token get_token() noexcept;
  [[nodiscard]] auto join() noexcept { return scope_.join(); }

  void close() noexcept { scope_.close(); }
  void request_stop() noexcept { stop_source_.request_stop(); }

 private:
  simple_counting_scope scope_;
  inplace_stop_source stop_source_;
};

class counting_scope::association {
 public:
  association() noexcept = default;

  association(const association&) = delete;
  association& operator=(const association&) = delete;
  association(association&&) noexcept = default;
  association& operator=(association&&) noexcept = default;

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(association_);
  }

  [[nodiscard]] association try_associate() const noexcept {
    if (!association_) {
      return {};
    }
    return association{association_.try_associate()};
  }

 private:
  friend class token;

  explicit association(simple_counting_scope::association association) noexcept
      : association_(std::move(association)) {}

  simple_counting_scope::association association_;
};

class counting_scope::token {
 public:
  token() = default;

  [[nodiscard]] association try_associate() const noexcept {
    return association{scope_token_.try_associate()};
  }

  void disassociate() noexcept { scope_token_.disassociate(); }

  template <bexec::sender Sender>
  [[nodiscard]] auto wrap(Sender&& sender) const noexcept(
      std::is_nothrow_constructible_v<detail::remove_cvref_t<Sender>, Sender>) {
    return detail::scope_stop_sender<detail::remove_cvref_t<Sender>>{
        std::forward<Sender>(sender), stop_token_};
  }

 private:
  friend class counting_scope;

  token(simple_counting_scope::token scope_token, inplace_stop_token stop_token)
      : scope_token_(std::move(scope_token)),
        stop_token_(std::move(stop_token)) {}

  simple_counting_scope::token scope_token_;
  inplace_stop_token stop_token_;
};

inline counting_scope::token counting_scope::get_token() noexcept {
  return token{scope_.get_token(), stop_source_.get_token()};
}

struct spawn_t {
  template <sender Sender, scope_token Token>
    requires detail::spawnable_sender_v<detail::remove_cvref_t<Sender>>
  void operator()(Sender&& sender, Token token) const {
    (*this)(std::forward<Sender>(sender), std::move(token), empty_env{});
  }

  template <sender Sender, scope_token Token, class Env>
    requires detail::spawnable_sender_v<detail::remove_cvref_t<Sender>> &&
             std::copy_constructible<detail::remove_cvref_t<Env>>
  void operator()(Sender&& sender, Token token, Env&& env) const {
    using env_type = detail::remove_cvref_t<Env>;
    env_type env_object{std::forward<Env>(env)};
    using byte_allocator = decltype(bexec::get_allocator(env_object));
    using association_type = decltype(token.try_associate());
    using wrapped_sender_type =
        detail::remove_cvref_t<decltype(detail::wrap_scope_sender(
            token, std::forward<Sender>(sender)))>;
    using operation_type =
        detail::spawn_operation<wrapped_sender_type, association_type, env_type,
                                byte_allocator>;
    using allocator_type = typename operation_type::allocator_type;
    using allocator_traits = std::allocator_traits<allocator_type>;

    auto association = token.try_associate();
    if (!association) {
      return;
    }

    auto wrapped_sender =
        detail::wrap_scope_sender(token, std::forward<Sender>(sender));
    byte_allocator byte_alloc = bexec::get_allocator(env_object);
    allocator_type allocator{byte_alloc};
    operation_type* operation = allocator_traits::allocate(allocator, 1);

#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      allocator_traits::construct(
          allocator, operation, std::move(wrapped_sender),
          std::move(association), std::move(env_object), allocator);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      allocator_traits::deallocate(allocator, operation, 1);
      throw;
    }
#endif

    operation->start();
  }
};

inline constexpr spawn_t spawn{};

struct spawn_future_t {
  template <sender Sender, scope_token Token>
  [[nodiscard]] auto operator()(Sender&& sender, Token token) const {
    return (*this)(std::forward<Sender>(sender), std::move(token), empty_env{});
  }

  template <sender Sender, scope_token Token, class Env>
  [[nodiscard]] auto operator()(Sender&& sender, Token token, Env&& env) const {
    using env_type = detail::remove_cvref_t<Env>;
    env_type env_object{std::forward<Env>(env)};
    auto wrapped_sender =
        detail::wrap_scope_sender(token, std::forward<Sender>(sender));

    using byte_allocator = decltype(bexec::get_allocator(env_object));
    using wrapped_sender_type =
        detail::remove_cvref_t<decltype(wrapped_sender)>;
    using token_type = detail::remove_cvref_t<Token>;
    using state_type =
        detail::spawn_future_state<wrapped_sender_type, token_type, env_type,
                                   byte_allocator>;
    using allocator_type = typename state_type::allocator_type;
    using allocator_traits = std::allocator_traits<allocator_type>;

    byte_allocator byte_alloc = bexec::get_allocator(env_object);
    allocator_type allocator{byte_alloc};
    state_type* state = allocator_traits::allocate(allocator, 1);

#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      allocator_traits::construct(allocator, state, std::move(byte_alloc),
                                  std::move(wrapped_sender), std::move(token),
                                  std::move(env_object));
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      allocator_traits::deallocate(allocator, state, 1);
      throw;
    }
#endif

    return detail::spawn_future_sender<state_type>{
        detail::spawn_future_state_handle<state_type>{state}};
  }
};

inline constexpr spawn_future_t spawn_future{};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_COUNTING_SCOPE_HPP_
