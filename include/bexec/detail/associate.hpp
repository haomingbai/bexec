/**
 * @file include/bexec/detail/associate.hpp
 * @brief Internal operations shared by associated senders and spawn.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-07-22
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Defines the private representation of associate(sender, token) and the
 * heap-backed state machines specified for spawn and spawn_future.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_ASSOCIATE_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_ASSOCIATE_HPP_

#include <atomic>
#include <bexec/completion_signatures.hpp>
#include <bexec/detail/config.hpp>
#include <bexec/detail/manual_lifetime.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/env.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/sender.hpp>
#include <bexec/stop_token.hpp>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace bexec::detail {

template <class BaseToken>
class scope_stop_token;

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

template <class Token, class Sender>
auto wrap_scope_sender(const Token& token, Sender&& sender) noexcept(
    noexcept(token.wrap(std::forward<Sender>(sender))))
    -> decltype(token.wrap(std::forward<Sender>(sender))) {
  return token.wrap(std::forward<Sender>(sender));
}

template <class Completions>
struct associated_completion_signatures;

template <class... Signatures>
struct associated_completion_signatures<completion_signatures<Signatures...>> {
  using type = completion_signatures_from_type_list_t<
      unique_type_list_t<concat_type_lists_t<type_list<Signatures...>,
                                             type_list<set_stopped_t()>>>>;
};

template <class Completions>
using associated_completion_signatures_t =
    typename associated_completion_signatures<Completions>::type;

template <class Token, class Sender>
class associate_data {
 public:
  using wrapped_sender_type = remove_cvref_t<decltype(wrap_scope_sender(
      std::declval<const Token&>(), std::declval<Sender>()))>;
  using association_type =
      remove_cvref_t<decltype(std::declval<Token&>().try_associate())>;

  associate_data(Token token, Sender&& sender) {
    sender_.emplace_from([&]() -> wrapped_sender_type {
      return wrap_scope_sender(token, std::forward<Sender>(sender));
    });
    association_ = token.try_associate();
    if (!association_) {
      sender_.reset();
    }
  }

  associate_data(const associate_data& other)
    requires std::copy_constructible<wrapped_sender_type>
      : association_(other.association_.try_associate()) {
    if (association_) {
      sender_.emplace(*other.sender_);
    }
  }

  associate_data& operator=(const associate_data&) = delete;

  associate_data(associate_data&& other) noexcept(
      std::is_nothrow_move_constructible_v<wrapped_sender_type>)
      : association_{} {
    if (other.sender_.has_value()) {
      sender_.emplace(std::move(*other.sender_));
    }
    association_ = std::move(other.association_);
    other.sender_.reset();
  }

  associate_data& operator=(associate_data&&) = delete;

  [[nodiscard]] bool has_sender() const noexcept { return sender_.has_value(); }

  [[nodiscard]] association_type release_association() noexcept {
    return std::move(association_);
  }

  [[nodiscard]] wrapped_sender_type&& release_sender() noexcept {
    return std::move(*sender_);
  }

  void reset_sender() noexcept { sender_.reset(); }

 private:
  association_type association_{};
  manual_lifetime<wrapped_sender_type> sender_;
};

template <class Token, class Sender>
class associated_sender {
 public:
  using data_type = associate_data<Token, Sender>;
  using wrapped_sender_type = typename data_type::wrapped_sender_type;
  using completion_signatures = associated_completion_signatures_t<
      bexec::completion_signatures_of_t<wrapped_sender_type>>;

  template <class Self, class Env>
  [[nodiscard]] static consteval auto get_completion_signatures() {
    return associated_completion_signatures_t<
        bexec::completion_signatures_of_t<wrapped_sender_type, Env>>{};
  }

  explicit associated_sender(data_type data) : data_(std::move(data)) {}

  associated_sender(const associated_sender&)
    requires std::copy_constructible<data_type>
  = default;

  associated_sender& operator=(const associated_sender&) = delete;
  associated_sender(associated_sender&&) noexcept = default;
  associated_sender& operator=(associated_sender&&) = delete;

  template <class Receiver>
  class operation {
   private:
    class child_receiver {
     public:
      explicit child_receiver(Receiver& receiver) noexcept
          : receiver_(&receiver) {}

      [[nodiscard]] auto get_env() const noexcept {
        return bexec::get_env(*receiver_);
      }

      template <class... Args>
      void set_value(Args&&... args) noexcept {
        bexec::set_value(std::move(*receiver_), std::forward<Args>(args)...);
      }

      template <class Error>
      void set_error(Error&& error) noexcept {
        bexec::set_error(std::move(*receiver_), std::forward<Error>(error));
      }

      void set_stopped() noexcept { bexec::set_stopped(std::move(*receiver_)); }

     private:
      Receiver* receiver_;
    };

    using child_operation_type = decltype(bexec::connect(
        std::declval<wrapped_sender_type>(), std::declval<child_receiver>()));

   public:
    operation(data_type data, Receiver receiver)
        : association_(data.release_association()),
          receiver_(std::move(receiver)) {
      if (data.has_sender()) {
        child_.emplace_from([this, &data]() -> child_operation_type {
          return bexec::connect(data.release_sender(),
                                child_receiver{receiver_});
        });
        data.reset_sender();
      }
    }

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    void start() noexcept {
      if (child_.has_value()) {
        bexec::start(*child_);
      } else {
        bexec::set_stopped(std::move(receiver_));
      }
    }

   private:
    typename data_type::association_type association_;
    Receiver receiver_;
    manual_lifetime<child_operation_type> child_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return operation<Receiver>{std::move(data_), std::move(receiver)};
  }

  template <class Receiver>
    requires std::copy_constructible<data_type>
  auto connect(Receiver receiver) const& {
    return operation<Receiver>{data_type{data_}, std::move(receiver)};
  }

 private:
  data_type data_;
};

template <class Token, class Sender>
auto make_associated_sender(Token token, Sender&& sender) {
  using token_type = remove_cvref_t<Token>;
  using sender_type = Sender;
  using data_type = associate_data<token_type, sender_type>;

  return associated_sender<token_type, sender_type>{
      data_type{std::move(token), std::forward<Sender>(sender)}};
}

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

template <class Operation>
class spawn_receiver {
 public:
  explicit spawn_receiver(Operation& operation) noexcept
      : operation_(&operation) {}

  [[nodiscard]] auto get_env() const noexcept(noexcept(operation_->env())) {
    return operation_->env();
  }

  void set_value() noexcept { operation_->complete(); }
  void set_stopped() noexcept { operation_->complete(); }

 private:
  Operation* operation_;
};

/**
 * @brief Heap-owned state specified by execution::spawn.
 *
 * The wrapped child is connected before try_associate() is evaluated. If
 * association succeeds, start() starts the already-connected operation;
 * otherwise it destroys the state without starting the child.
 */
template <class Sender, class Token, class Env, class ByteAllocator>
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
  using association_type =
      remove_cvref_t<decltype(std::declval<Token&>().try_associate())>;

  spawn_operation(Sender sender, Token token, Env env, allocator_type allocator)
      : env_(std::move(env)),
        allocator_(std::move(allocator)),
        operation_(bexec::connect(std::move(sender), receiver_type{*this})),
        association_(token.try_associate()) {}

  spawn_operation(const spawn_operation&) = delete;
  spawn_operation& operator=(const spawn_operation&) = delete;
  spawn_operation(spawn_operation&&) = delete;
  spawn_operation& operator=(spawn_operation&&) = delete;

  void start() noexcept {
    if (!association_) {
      complete();
      return;
    }

    bool complete_now = false;
    {
      std::lock_guard lock(mutex_);
      starting_ = true;
    }
    bexec::start(operation_);
    {
      std::lock_guard lock(mutex_);
      starting_ = false;
      complete_now = completed_;
    }

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
    association_type association = std::move(association_);
    allocator_type allocator = allocator_;
    spawn_operation* self = this;
    allocator_traits::destroy(allocator, self);
    allocator_traits::deallocate(allocator, self, 1);
  }

  Env env_;
  allocator_type allocator_;
  child_operation_type operation_;
  association_type association_;
  std::mutex mutex_;
  bool starting_{false};
  bool completed_{false};
};

template <class Sender, class Token, class Env>
void spawn(Sender&& sender, Token token, Env&& env) {
  using token_type = remove_cvref_t<Token>;
  using env_type = remove_cvref_t<Env>;
  env_type env_object{std::forward<Env>(env)};
  auto wrapped_sender = wrap_scope_sender(token, std::forward<Sender>(sender));
  using wrapped_sender_type = remove_cvref_t<decltype(wrapped_sender)>;
  using byte_allocator = decltype(bexec::get_allocator(env_object));
  using operation_type = spawn_operation<wrapped_sender_type, token_type,
                                         env_type, byte_allocator>;
  using allocator_type = typename operation_type::allocator_type;
  using allocator_traits = std::allocator_traits<allocator_type>;

  byte_allocator byte_alloc = bexec::get_allocator(env_object);
  allocator_type allocator{byte_alloc};
  operation_type* operation = allocator_traits::allocate(allocator, 1);

#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
  try {
#endif
    allocator_traits::construct(allocator, operation, std::move(wrapped_sender),
                                std::move(token), std::move(env_object),
                                allocator);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
  } catch (...) {
    allocator_traits::deallocate(allocator, operation, 1);
    throw;
  }
#endif

  bexec::start(*operation);
}

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

  spawn_future_state(byte_allocator_type byte_allocator, Sender sender,
                     Token token, Env env)
      : byte_allocator_(std::move(byte_allocator)),
        env_(std::move(env)),
        operation_(bexec::connect(
            std::move(sender),
            receiver_type{this, stop_source_.get_token(), env_})),
        association_(token.try_associate()) {
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
      if (cancelling_) {
        return;
      }
      if (stopped_ || abandoned_) {
        destroy = true;
      } else if (consumer_ != nullptr) {
        receiver = consumer_;
        deliver = deliver_;
        consumer_ = nullptr;
      }
    }

    if (deliver != nullptr) {
      deliver(this, receiver);
      destroy_self();
    } else if (destroy) {
      destroy_self();
    }
  }

  template <class Receiver>
  void consume(Receiver& receiver) noexcept {
    bool deliver_result = false;
    bool deliver_stopped = false;
    {
      std::lock_guard lock(mutex_);
      if (completed_) {
        deliver_result = true;
      } else if (stopped_) {
        deliver_stopped = true;
      } else {
        consumer_ = &receiver;
        deliver_ = deliver_to_receiver<Receiver>;
        deliver_stopped_ = deliver_stopped_to_receiver<Receiver>;
      }
    }

    if (deliver_result) {
      deliver_to_receiver<Receiver>(this, &receiver);
      destroy_self();
    } else if (deliver_stopped) {
      bexec::set_stopped(std::move(receiver));
    }
  }

  void try_cancel() noexcept {
    {
      std::lock_guard lock(mutex_);
      if (completed_ || stopped_) {
        return;
      }
      cancelling_ = true;
    }

    stop_source_.request_stop();
    finish_cancellation();
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
  void finish_cancellation() noexcept {
    void* receiver = nullptr;
    void (*deliver)(spawn_future_state*, void*) noexcept = nullptr;
    void (*deliver_stopped)(void*) noexcept = nullptr;
    bool destroy = false;
    {
      std::lock_guard lock(mutex_);
      if (!cancelling_) {
        return;
      }

      cancelling_ = false;
      if (completed_) {
        if (abandoned_ || stopped_) {
          destroy = true;
        } else if (consumer_ != nullptr) {
          receiver = consumer_;
          deliver = deliver_;
          consumer_ = nullptr;
        }
      } else {
        stopped_ = true;
      }

      if (!completed_ && consumer_ != nullptr) {
        receiver = consumer_;
        deliver_stopped = deliver_stopped_;
        consumer_ = nullptr;
      }
    }

    if (deliver != nullptr) {
      deliver(this, receiver);
      destroy_self();
    } else if (deliver_stopped != nullptr) {
      deliver_stopped(receiver);
    } else if (destroy) {
      destroy_self();
    }
  }

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

  template <class Receiver>
  static void deliver_stopped_to_receiver(void* raw_receiver) noexcept {
    Receiver& receiver = *static_cast<Receiver*>(raw_receiver);
    bexec::set_stopped(std::move(receiver));
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
  void (*deliver_stopped_)(void*) noexcept {nullptr};
  bool completed_{false};
  bool stopped_{false};
  bool abandoned_{false};
  bool cancelling_{false};
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
  [[nodiscard]] State* release() noexcept {
    return std::exchange(state_, nullptr);
  }

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
   private:
    struct cancel_callback {
      State* state;

      void operator()() const noexcept { state->try_cancel(); }
    };

    using stop_token_type = decltype(bexec::get_stop_token(
        bexec::get_env(std::declval<Receiver&>())));
    using stop_callback_type =
        typename stop_token_type::template callback_type<cancel_callback>;

    class future_receiver {
     public:
      future_receiver(Receiver& receiver,
                      std::optional<stop_callback_type>& stop_callback) noexcept
          : receiver_(&receiver), stop_callback_(&stop_callback) {}

      [[nodiscard]] auto get_env() const noexcept {
        return bexec::get_env(*receiver_);
      }

      template <class... Args>
      void set_value(Args&&... args) noexcept {
        stop_callback_->reset();
        bexec::set_value(std::move(*receiver_), std::forward<Args>(args)...);
      }

      template <class Error>
      void set_error(Error&& error) noexcept {
        stop_callback_->reset();
        bexec::set_error(std::move(*receiver_), std::forward<Error>(error));
      }

      void set_stopped() noexcept {
        stop_callback_->reset();
        bexec::set_stopped(std::move(*receiver_));
      }

     private:
      Receiver* receiver_;
      std::optional<stop_callback_type>* stop_callback_;
    };

   public:
    operation(spawn_future_state_handle<State> state, Receiver receiver)
        : state_(std::move(state)),
          receiver_(std::move(receiver)),
          future_receiver_(receiver_, stop_callback_) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    void start() noexcept {
      State* state = state_.release();
      stop_callback_.emplace(bexec::get_stop_token(bexec::get_env(receiver_)),
                             cancel_callback{state});
      state->consume(future_receiver_);
    }

   private:
    spawn_future_state_handle<State> state_;
    Receiver receiver_;
    std::optional<stop_callback_type> stop_callback_;
    future_receiver future_receiver_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return operation<Receiver>{std::move(state_), std::move(receiver)};
  }

 private:
  spawn_future_state_handle<State> state_;
};

template <class Sender, class Token, class Env>
auto spawn_future(Sender&& sender, Token token, Env&& env) {
  using token_type = remove_cvref_t<Token>;
  using env_type = remove_cvref_t<Env>;
  env_type env_object{std::forward<Env>(env)};
  auto wrapped_sender = wrap_scope_sender(token, std::forward<Sender>(sender));
  using wrapped_sender_type = remove_cvref_t<decltype(wrapped_sender)>;
  using byte_allocator = decltype(bexec::get_allocator(env_object));
  using state_type = spawn_future_state<wrapped_sender_type, token_type,
                                        env_type, byte_allocator>;
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

  return spawn_future_sender<state_type>{
      spawn_future_state_handle<state_type>{state}};
}

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_ASSOCIATE_HPP_
