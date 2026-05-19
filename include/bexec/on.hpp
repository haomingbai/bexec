/**
 * @file include/bexec/on.hpp
 * @brief Public scheduling adaptors starts_on and on.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-19
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_ON_HPP_
#define BEXEC_INCLUDE_BEXEC_ON_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/concepts.hpp>
#include <bexec/detail/config.hpp>
#include <bexec/detail/manual_lifetime.hpp>
#include <bexec/detail/type_traits.hpp>
#include <bexec/env.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/receiver.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace bexec {

template <class Scheduler, class Sender>
class starts_on_sender;

namespace detail {

template <class Scheduler>
using schedule_sender_for_t =
    remove_cvref_t<decltype(bexec::schedule(std::declval<Scheduler&>()))>;

template <class Scheduler>
using schedule_error_signature_list_t = set_error_signatures_from_type_list_t<
    sender_error_types_t<schedule_sender_for_t<Scheduler>>>;

template <class Scheduler>
using schedule_stopped_signature_list_t =
    std::conditional_t<sender_sends_stopped_v<schedule_sender_for_t<Scheduler>>,
                       type_list<set_stopped_t()>, type_list<>>;

template <class Scheduler, class Sender>
struct starts_on_completion_signatures {
  using signatures = unique_type_list_t<
      concat_type_lists_t<completion_signatures_to_type_list_t<
                              sender_completion_signatures_t<Sender>>,
                          schedule_error_signature_list_t<Scheduler>,
                          type_list<set_error_t(std::exception_ptr)>,
                          schedule_stopped_signature_list_t<Scheduler>>>;
  using type = completion_signatures_from_type_list_t<signatures>;
};

template <class Scheduler, class Sender>
using starts_on_completion_signatures_t =
    typename starts_on_completion_signatures<Scheduler, Sender>::type;

template <class Operation>
class starts_on_schedule_receiver {
 public:
  explicit starts_on_schedule_receiver(Operation& operation)
      : operation_(&operation) {}

  [[nodiscard]] auto get_env() const
      noexcept(noexcept(bexec::get_env(operation_->receiver()))) {
    return bexec::get_env(operation_->receiver());
  }

  void set_value() noexcept { operation_->start_child(); }

  template <class Error>
  void set_error(Error&& error) noexcept {
    operation_->schedule_error(std::forward<Error>(error));
  }

  void set_stopped() noexcept { operation_->schedule_stopped(); }

 private:
  Operation* operation_;
};

template <class Operation>
class starts_on_child_receiver {
 public:
  explicit starts_on_child_receiver(Operation& operation)
      : operation_(&operation) {}

  [[nodiscard]] auto get_env() const noexcept {
    return env_with_scheduler{operation_->scheduler(),
                              bexec::get_env(operation_->receiver())};
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
    bexec::set_value(std::move(operation_->receiver()),
                     std::forward<Args>(args)...);
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    bexec::set_error(std::move(operation_->receiver()),
                     std::forward<Error>(error));
  }

  void set_stopped() noexcept {
    bexec::set_stopped(std::move(operation_->receiver()));
  }

 private:
  Operation* operation_;
};

template <class Tuple>
struct value_completion {
  explicit value_completion(Tuple tuple) : values(std::move(tuple)) {}
  Tuple values;
};

template <class Error>
struct error_completion {
  explicit error_completion(Error error_value)
      : error(std::move(error_value)) {}
  Error error;
};

struct stopped_completion {};

template <class Signature>
struct completion_variant_alternative {
  using type = type_list<>;
};

template <class... Args>
struct completion_variant_alternative<set_value_t(Args...)> {
  using type = type_list<value_completion<decayed_tuple<Args...>>>;
};

template <class Error>
struct completion_variant_alternative<set_error_t(Error)> {
  using type = type_list<error_completion<std::decay_t<Error>>>;
};

template <>
struct completion_variant_alternative<set_stopped_t()> {
  using type = type_list<stopped_completion>;
};

template <class Completions>
struct completion_variant_type_list;

template <class... Signatures>
struct completion_variant_type_list<completion_signatures<Signatures...>> {
  using type = unique_type_list_t<concat_type_lists_t<
      typename completion_variant_alternative<Signatures>::type...>>;
};

template <class List>
struct variant_from_completion_alternatives;

template <class... Alternatives>
struct variant_from_completion_alternatives<type_list<Alternatives...>> {
  using type = std::variant<Alternatives...>;
};

template <class Completions>
using completion_variant_t = typename variant_from_completion_alternatives<
    typename completion_variant_type_list<Completions>::type>::type;

template <class Receiver>
using receiver_scheduler_t = remove_cvref_t<decltype(bexec::get_scheduler(
    bexec::get_env(std::declval<Receiver&>())))>;

template <class Scheduler, class Sender>
struct on_completion_signatures {
  using source_completions =
      starts_on_completion_signatures_t<Scheduler, Sender>;
  using signatures = unique_type_list_t<concat_type_lists_t<
      completion_signatures_to_type_list_t<source_completions>,
      type_list<set_error_t(std::exception_ptr), set_stopped_t()>>>;
  using type = completion_signatures_from_type_list_t<signatures>;
};

template <class Scheduler, class Sender>
using on_completion_signatures_t =
    typename on_completion_signatures<Scheduler, Sender>::type;

template <class Operation>
class on_child_receiver {
 public:
  explicit on_child_receiver(Operation& operation) : operation_(&operation) {}

  [[nodiscard]] auto get_env() const noexcept {
    return env_with_scheduler{operation_->target_scheduler(),
                              bexec::get_env(operation_->receiver())};
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
    operation_->store_value(std::forward<Args>(args)...);
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    operation_->store_error(std::forward<Error>(error));
  }

  void set_stopped() noexcept { operation_->store_stopped(); }

 private:
  Operation* operation_;
};

template <class Operation>
class on_final_receiver {
 public:
  explicit on_final_receiver(Operation& operation) : operation_(&operation) {}

  [[nodiscard]] auto get_env() const
      noexcept(noexcept(bexec::get_env(operation_->receiver()))) {
    return bexec::get_env(operation_->receiver());
  }

  void set_value() noexcept { operation_->deliver_stored(); }

  template <class Error>
  void set_error(Error&& error) noexcept {
    bexec::set_error(std::move(operation_->receiver()),
                     std::forward<Error>(error));
  }

  void set_stopped() noexcept {
    bexec::set_stopped(std::move(operation_->receiver()));
  }

 private:
  Operation* operation_;
};

}  // namespace detail

template <class Scheduler, class Sender>
class starts_on_sender {
 public:
  using completion_signatures =
      detail::starts_on_completion_signatures_t<Scheduler, Sender>;

  template <class SchedulerArg, class SenderArg>
    requires std::constructible_from<Scheduler, SchedulerArg> &&
                 std::constructible_from<Sender, SenderArg>
  starts_on_sender(SchedulerArg&& scheduler, SenderArg&& sender)
      : scheduler_(std::forward<SchedulerArg>(scheduler)),
        sender_(std::forward<SenderArg>(sender)) {}

  template <class Receiver>
  class operation {
   public:
    using operation_type = operation;
    using schedule_sender_type = detail::schedule_sender_for_t<Scheduler>;
    using schedule_receiver_type =
        detail::starts_on_schedule_receiver<operation_type>;
    using schedule_operation_type =
        decltype(bexec::connect(std::declval<schedule_sender_type>(),
                                std::declval<schedule_receiver_type>()));
    using child_receiver_type =
        detail::starts_on_child_receiver<operation_type>;
    using child_operation_type = decltype(bexec::connect(
        std::declval<Sender>(), std::declval<child_receiver_type>()));

    operation(Scheduler scheduler, Sender sender, Receiver receiver)
        : scheduler_(std::move(scheduler)),
          sender_(std::move(sender)),
          receiver_(std::move(receiver)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    Receiver& receiver() noexcept { return receiver_; }
    Scheduler scheduler() const { return scheduler_; }

    void start() noexcept { start_schedule(); }

    void start_schedule() noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      try {
#endif
        schedule_operation_.emplace_from([this]() -> schedule_operation_type {
          return bexec::connect(bexec::schedule(scheduler_),
                                schedule_receiver_type{*this});
        });
        bexec::start(*schedule_operation_);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      } catch (...) {
        bexec::set_error(std::move(receiver_), std::current_exception());
      }
#endif
    }

    void start_child() noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      try {
#endif
        child_operation_.emplace_from([this]() -> child_operation_type {
          return bexec::connect(std::move(sender_), child_receiver_type{*this});
        });
        bexec::start(*child_operation_);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      } catch (...) {
        bexec::set_error(std::move(receiver_), std::current_exception());
      }
#endif
    }

    template <class Error>
    void schedule_error(Error&& error) noexcept {
      bexec::set_error(std::move(receiver_), std::forward<Error>(error));
    }

    void schedule_stopped() noexcept {
      bexec::set_stopped(std::move(receiver_));
    }

   private:
    Scheduler scheduler_;
    Sender sender_;
    Receiver receiver_;
    detail::manual_lifetime<schedule_operation_type> schedule_operation_;
    detail::manual_lifetime<child_operation_type> child_operation_;
  };

  template <class Receiver>
  auto connect(Receiver receiver) && {
    return operation<Receiver>{std::move(scheduler_), std::move(sender_),
                               std::move(receiver)};
  }

  template <class Receiver>
    requires std::copy_constructible<Sender>
  auto connect(Receiver receiver) const& {
    return operation<Receiver>{scheduler_, sender_, std::move(receiver)};
  }

 private:
  Scheduler scheduler_;
  Sender sender_;
};

template <class Scheduler, class Sender>
class on_sender {
 public:
  using completion_signatures =
      detail::on_completion_signatures_t<Scheduler, Sender>;

  template <class SchedulerArg, class SenderArg>
    requires std::constructible_from<Scheduler, SchedulerArg> &&
                 std::constructible_from<Sender, SenderArg>
  on_sender(SchedulerArg&& scheduler, SenderArg&& sender)
      : scheduler_(std::forward<SchedulerArg>(scheduler)),
        sender_(std::forward<SenderArg>(sender)) {}

  template <class Receiver>
  class operation {
   public:
    using operation_type = operation;
    using final_scheduler_type = detail::receiver_scheduler_t<Receiver>;
    using source_sender_type = starts_on_sender<Scheduler, Sender>;
    using source_receiver_type = detail::on_child_receiver<operation_type>;
    using source_operation_type =
        decltype(bexec::connect(std::declval<source_sender_type>(),
                                std::declval<source_receiver_type>()));
    using final_schedule_sender_type =
        detail::schedule_sender_for_t<final_scheduler_type>;
    using final_receiver_type = detail::on_final_receiver<operation_type>;
    using final_operation_type =
        decltype(bexec::connect(std::declval<final_schedule_sender_type>(),
                                std::declval<final_receiver_type>()));
    using completion_variant = detail::completion_variant_t<
        typename source_sender_type::completion_signatures>;

    operation(Scheduler scheduler, Sender sender, Receiver receiver)
        : target_scheduler_(std::move(scheduler)),
          final_scheduler_(bexec::get_scheduler(bexec::get_env(receiver))),
          sender_(std::move(sender)),
          receiver_(std::move(receiver)) {}

    operation(const operation&) = delete;
    operation& operator=(const operation&) = delete;
    operation(operation&&) = delete;
    operation& operator=(operation&&) = delete;

    Receiver& receiver() noexcept { return receiver_; }
    Scheduler target_scheduler() const { return target_scheduler_; }

    void start() noexcept { start_source(); }

    void start_source() noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      try {
#endif
        source_operation_.emplace_from([this]() -> source_operation_type {
          return bexec::connect(
              source_sender_type{target_scheduler_, std::move(sender_)},
              source_receiver_type{*this});
        });
        bexec::start(*source_operation_);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      } catch (...) {
        bexec::set_error(std::move(receiver_), std::current_exception());
      }
#endif
    }

    template <class... Args>
    void store_value(Args&&... args) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      try {
#endif
        using tuple_type = detail::decayed_tuple<Args...>;
        stored_.emplace(
            std::in_place_type<detail::value_completion<tuple_type>>,
            tuple_type{std::forward<Args>(args)...});
        start_final();
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      } catch (...) {
        bexec::set_error(std::move(receiver_), std::current_exception());
      }
#endif
    }

    template <class Error>
    void store_error(Error&& error) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      try {
#endif
        using error_type = std::decay_t<Error>;
        stored_.emplace(
            std::in_place_type<detail::error_completion<error_type>>,
            std::forward<Error>(error));
        start_final();
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      } catch (...) {
        bexec::set_error(std::move(receiver_), std::current_exception());
      }
#endif
    }

    void store_stopped() noexcept {
      stored_.emplace(std::in_place_type<detail::stopped_completion>);
      start_final();
    }

    void start_final() noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      try {
#endif
        final_operation_.emplace_from([this]() -> final_operation_type {
          return bexec::connect(bexec::schedule(final_scheduler_),
                                final_receiver_type{*this});
        });
        bexec::start(*final_operation_);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
      } catch (...) {
        bexec::set_error(std::move(receiver_), std::current_exception());
      }
#endif
    }

    void deliver_stored() noexcept {
      std::visit([this](auto& completion) noexcept { deliver_one(completion); },
                 *stored_);
    }

    template <class Tuple>
    void deliver_one(detail::value_completion<Tuple>& completion) noexcept {
      std::apply(
          [this](auto&&... args) noexcept {
            bexec::set_value(std::move(receiver_),
                             std::forward<decltype(args)>(args)...);
          },
          std::move(completion.values));
    }

    template <class Error>
    void deliver_one(detail::error_completion<Error>& completion) noexcept {
      bexec::set_error(std::move(receiver_), std::move(completion.error));
    }

    void deliver_one(detail::stopped_completion&) noexcept {
      bexec::set_stopped(std::move(receiver_));
    }

   private:
    Scheduler target_scheduler_;
    final_scheduler_type final_scheduler_;
    Sender sender_;
    Receiver receiver_;
    std::optional<completion_variant> stored_;
    detail::manual_lifetime<source_operation_type> source_operation_;
    detail::manual_lifetime<final_operation_type> final_operation_;
  };

  template <class Receiver>
    requires requires(Receiver& receiver) {
      bexec::get_scheduler(bexec::get_env(receiver));
    }
  auto connect(Receiver receiver) && {
    return operation<Receiver>{std::move(scheduler_), std::move(sender_),
                               std::move(receiver)};
  }

  template <class Receiver>
    requires std::copy_constructible<Sender> && requires(Receiver& receiver) {
      bexec::get_scheduler(bexec::get_env(receiver));
    }
  auto connect(Receiver receiver) const& {
    return operation<Receiver>{scheduler_, sender_, std::move(receiver)};
  }

 private:
  Scheduler scheduler_;
  Sender sender_;
};

/**
 * @brief Function object that creates starts_on(scheduler, sender).
 */
struct starts_on_t {
  template <scheduler Scheduler, sender Sender>
  [[nodiscard]] auto operator()(Scheduler&& scheduler, Sender&& sender) const {
    return starts_on_sender<detail::remove_cvref_t<Scheduler>,
                            detail::remove_cvref_t<Sender>>{
        std::forward<Scheduler>(scheduler), std::forward<Sender>(sender)};
  }
};

/**
 * @brief Function object that creates on(scheduler, sender).
 */
struct on_t {
  template <scheduler Scheduler, sender Sender>
  [[nodiscard]] auto operator()(Scheduler&& scheduler, Sender&& sender) const {
    return on_sender<detail::remove_cvref_t<Scheduler>,
                     detail::remove_cvref_t<Sender>>{
        std::forward<Scheduler>(scheduler), std::forward<Sender>(sender)};
  }
};

inline constexpr starts_on_t starts_on{};
inline constexpr on_t on{};

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_ON_HPP_
