/**
 * @file include/bexec/detail/manual_lifetime.hpp
 * @brief Internal storage for explicitly managed object lifetime.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-13
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides optional-like storage that can construct non-movable operation
 * states directly from a factory result.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_MANUAL_LIFETIME_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_MANUAL_LIFETIME_HPP_

#include <new>
#include <utility>

namespace bexec::detail {

template <class T>
class manual_lifetime {
 public:
  manual_lifetime() noexcept = default;

  manual_lifetime(const manual_lifetime&) = delete;
  manual_lifetime& operator=(const manual_lifetime&) = delete;
  manual_lifetime(manual_lifetime&&) = delete;
  manual_lifetime& operator=(manual_lifetime&&) = delete;

  ~manual_lifetime() { reset(); }

  template <class... Args>
  T& emplace(Args&&... args) {
    reset();
    T* object =
        ::new (static_cast<void*>(storage_)) T(std::forward<Args>(args)...);
    engaged_ = true;
    return *object;
  }

  template <class Factory>
  T& emplace_from(Factory&& factory) {
    reset();
    T* object = ::new (static_cast<void*>(storage_))
        T(std::forward<Factory>(factory)());
    engaged_ = true;
    return *object;
  }

  void reset() noexcept {
    if (!engaged_) {
      return;
    }

    get()->~T();
    engaged_ = false;
  }

  [[nodiscard]] bool has_value() const noexcept { return engaged_; }

  T& operator*() noexcept { return *get(); }
  const T& operator*() const noexcept { return *get(); }

  T* operator->() noexcept { return get(); }
  const T* operator->() const noexcept { return get(); }

 private:
  T* get() noexcept {
    return std::launder(reinterpret_cast<T*>(static_cast<void*>(storage_)));
  }

  const T* get() const noexcept {
    return std::launder(
        reinterpret_cast<const T*>(static_cast<const void*>(storage_)));
  }

  alignas(T) unsigned char storage_[sizeof(T)];
  bool engaged_{false};
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_MANUAL_LIFETIME_HPP_
