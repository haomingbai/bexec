/**
 * @file include/bexec/counting_scope.hpp
 * @brief Standard-style counting scopes and spawn consumer CPOs.
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
#include <bexec/associate.hpp>
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
#include <cassert>
#include <concepts>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace bexec {

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
    word_.fetch_or(closed_bit, std::memory_order_release);
  }

 private:
  // The association count and the scope state are packed into a single atomic
  // word, word = (count << 3) | state_bits, so that every read-modify-write
  // validates the entire (count, state) pair with one CAS. "Unused" is simply
  // word_ == 0.
  static constexpr std::size_t closed_bit = 1;        // 0b001
  static constexpr std::size_t join_needed_bit = 2;   // 0b010
  static constexpr std::size_t join_running_bit = 4;  // 0b100
  static constexpr std::size_t max_associations =
      std::numeric_limits<std::size_t>::max() >> 3;

  static constexpr std::size_t count_of(std::size_t word) noexcept {
    return word >> 3;
  }

  static constexpr std::size_t state_of(std::size_t word) noexcept {
    return word & 7;
  }

  static constexpr std::size_t make_word(std::size_t count,
                                         std::size_t state) noexcept {
    return (count << 3) | state;
  }

  static constexpr bool is_closed(std::size_t word) noexcept {
    return (word & closed_bit) != 0;
  }

  static constexpr bool is_joining(std::size_t word) noexcept {
    return (word & join_running_bit) != 0;
  }

  static constexpr bool is_joined(std::size_t word) noexcept {
    return word == closed_bit;
  }

  friend class token;
  friend class join_sender;
  friend class counting_scope;

  void ensure_destructible() noexcept {
    // Deliberately more permissive than the C++26 contract: drained open and
    // closed scopes (count == 0, no join in flight) stay destructible, which
    // preserves the historical bexec behavior.
    const std::size_t bits = word_.load(std::memory_order_acquire);
    if ((bits & join_running_bit) != 0 || count_of(bits) != 0) {
      std::terminate();
    }
  }

  [[nodiscard]] bool try_associate() noexcept {
    std::size_t bits = word_.load(std::memory_order_acquire);
    for (;;) {
      if (is_closed(bits) || count_of(bits) == max_associations) {
        return false;  // no effect
      }
      // state_of(bits) | join_needed_bit maps 0 -> 2, 2 -> 2 and 6 -> 6.
      const std::size_t next =
          make_word(count_of(bits) + 1, state_of(bits) | join_needed_bit);
      if (word_.compare_exchange_weak(bits, next, std::memory_order_acquire,
                                      std::memory_order_acquire)) {
        return true;
      }
    }
  }

  void disassociate() noexcept {
    std::size_t bits = word_.load(std::memory_order_relaxed);
    for (;;) {
      assert(count_of(bits) > 0);
      const std::size_t count = count_of(bits) - 1;
      // Reaching zero while joining becomes joined in this very CAS, dropping
      // join_needed_bit and join_running_bit in the same write.
      const std::size_t next = (count == 0 && is_joining(bits))
                                   ? closed_bit
                                   : make_word(count, state_of(bits));
      if (word_.compare_exchange_weak(bits, next, std::memory_order_acq_rel,
                                      std::memory_order_relaxed)) {
        if (is_joined(next)) {
          complete_registered_joins();
        }
        return;
      }
    }
  }

  bool start_join(detail::scope_join_waiter& waiter) noexcept {
    std::size_t bits = word_.load(std::memory_order_relaxed);
    for (;;) {
      if (count_of(bits) == 0) {
        // No outstanding work: force joined regardless of open/closed/unused.
        // This also closes an open empty scope.
        if (word_.compare_exchange_weak(bits, closed_bit,
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
          return true;  // complete inline
        }
        continue;  // an associate raced in; re-evaluate
      }
      // count > 0: mark joining. join_running_bit is a flag, not a counter;
      // concurrent joiners OR the same bit idempotently.
      if (word_.compare_exchange_weak(bits, bits | join_running_bit,
                                      std::memory_order_relaxed,
                                      std::memory_order_relaxed)) {
        return !register_joiner(waiter);
      }
    }
  }

  bool register_joiner(detail::scope_join_waiter& waiter) noexcept {
    // Returns false iff the list was already drained (sentinel observed); the
    // caller completes the join inline and the node is never linked.
    auto* head = joiners_.load(std::memory_order_acquire);
    for (;;) {
      if (head == joiners_sentinel()) {
        return false;
      }
      waiter.next = head;
      if (joiners_.compare_exchange_weak(head, &waiter,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
        return true;
      }
    }
  }

  void complete_registered_joins() noexcept {
    // Called exactly once, by the disassociate whose CAS produced
    // word_ == closed_bit. The exchange both empties the list and installs
    // the terminal sentinel, so later registrations complete inline.
    auto* list =
        joiners_.exchange(joiners_sentinel(), std::memory_order_acq_rel);
    while (list != nullptr) {
      // Advance the cursor before completing: complete_deferred() starts the
      // join receiver's scheduled completion and the waiter (a subobject of
      // the join operation state) may be destroyed before the loop reads
      // next again.
      auto* waiter = std::exchange(list, list->next);
      waiter->complete_deferred();
    }
  }

  detail::scope_join_waiter* joiners_sentinel() noexcept {
    // The scope's own address serves as the drained sentinel; it is only ever
    // compared against a head pointer, never dereferenced.
    return reinterpret_cast<detail::scope_join_waiter*>(this);
  }

  std::atomic<std::size_t> word_{0};  // (count << 3) | state bits; 0 == unused
  std::atomic<detail::scope_join_waiter*> joiners_{nullptr};
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
    if (scope_ == nullptr || !scope_->try_associate()) {
      return {};
    }
    return association{*scope_};
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

class simple_counting_scope::token {
 public:
  token() = default;

  [[nodiscard]] association try_associate() const noexcept {
    if (scope_ == nullptr || !scope_->try_associate()) {
      return {};
    }
    return association{*scope_};
  }

  template <bexec::sender Sender>
  [[nodiscard]] Sender&& wrap(Sender&& sender) const noexcept {
    return std::forward<Sender>(sender);
  }

  void disassociate() noexcept {
    assert(scope_ != nullptr);
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
  class operation final : private detail::scope_join_waiter {
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
      if constexpr (noexcept(
                        bexec::connect(std::declval<schedule_sender_type>(),
                                       std::declval<final_receiver_type>()))) {
        final_operation_.emplace_from([this]() -> final_operation_type {
          return bexec::connect(bexec::schedule(scheduler_),
                                final_receiver_type{*this});
        });
        bexec::start(*final_operation_);
      } else {
        try {
          final_operation_.emplace_from([this]() -> final_operation_type {
            return bexec::connect(bexec::schedule(scheduler_),
                                  final_receiver_type{*this});
          });
          bexec::start(*final_operation_);
        } catch (...) {
          bexec::set_error(std::move(receiver_), std::current_exception());
        }
      }
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
    requires detail::spawnable_sender_v<
                 detail::remove_cvref_t<decltype(detail::wrap_scope_sender(
                     std::declval<const Token&>(), std::declval<Sender>()))>> &&
             std::constructible_from<detail::remove_cvref_t<Sender>, Sender>
  void operator()(Sender&& sender, Token token) const {
    detail::spawn(std::forward<Sender>(sender), std::move(token), empty_env{});
  }

  template <sender Sender, scope_token Token, class Env>
    requires detail::spawnable_sender_v<
                 detail::remove_cvref_t<decltype(detail::wrap_scope_sender(
                     std::declval<const Token&>(), std::declval<Sender>()))>> &&
             std::copy_constructible<detail::remove_cvref_t<Env>> &&
             std::constructible_from<detail::remove_cvref_t<Sender>, Sender>
  void operator()(Sender&& sender, Token token, Env&& env) const {
    detail::spawn(std::forward<Sender>(sender), std::move(token),
                  std::forward<Env>(env));
  }
};

inline constexpr spawn_t spawn{};

struct spawn_future_t {
  template <sender Sender, scope_token Token>
    requires std::constructible_from<detail::remove_cvref_t<Sender>, Sender>
  [[nodiscard]] auto operator()(Sender&& sender, Token token) const {
    return detail::spawn_future(std::forward<Sender>(sender), std::move(token),
                                empty_env{});
  }

  template <sender Sender, scope_token Token, class Env>
    requires std::constructible_from<detail::remove_cvref_t<Sender>, Sender>
  [[nodiscard]] auto operator()(Sender&& sender, Token token, Env&& env) const {
    return detail::spawn_future(std::forward<Sender>(sender), std::move(token),
                                std::forward<Env>(env));
  }
};

inline constexpr spawn_future_t spawn_future{};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_COUNTING_SCOPE_HPP_
