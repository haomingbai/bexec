#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_THEN_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_THEN_HPP_

#include <bexec/detail/config.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace bexec::detail {

template <class Receiver, class Fn>
class then_receiver {
 public:
  then_receiver(Receiver receiver, Fn fn)
      : receiver_(std::move(receiver)), fn_(std::move(fn)) {}

  [[nodiscard]] auto get_env() const
      noexcept(noexcept(bexec::get_env(receiver_))) {
    return bexec::get_env(receiver_);
  }

  template <class... Args>
  void set_value(Args&&... args) noexcept {
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    try {
#endif
      complete_value(std::forward<Args>(args)...);
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
    } catch (...) {
      bexec::set_error(std::move(receiver_), std::current_exception());
    }
#endif
  }

  template <class Error>
  void set_error(Error&& error) noexcept {
    bexec::set_error(std::move(receiver_), std::forward<Error>(error));
  }

  void set_stopped() noexcept { bexec::set_stopped(std::move(receiver_)); }

 private:
  template <class... Args>
  void complete_value(Args&&... args) {
    if constexpr (std::is_void_v<std::invoke_result_t<Fn&, Args...>>) {
      std::invoke(fn_, std::forward<Args>(args)...);
      bexec::set_value(std::move(receiver_));
    } else {
      bexec::set_value(std::move(receiver_),
                       std::invoke(fn_, std::forward<Args>(args)...));
    }
  }

  Receiver receiver_;
  Fn fn_;
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_THEN_HPP_
