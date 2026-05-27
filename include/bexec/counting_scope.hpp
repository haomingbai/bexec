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
#include <bexec/detail/counting_scope.hpp>
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
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace bexec {

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
