/**
 * @file include/bexec/detail/erased_lifetime.hpp
 * @brief In-place lifetime storage for one erased non-movable object.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-23
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_ERASED_LIFETIME_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_ERASED_LIFETIME_HPP_

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace bexec::detail {

template <std::size_t Size, std::size_t Align = alignof(std::max_align_t)>
class erased_lifetime {
 public:
  erased_lifetime() noexcept = default;

  erased_lifetime(const erased_lifetime&) = delete;
  erased_lifetime& operator=(const erased_lifetime&) = delete;
  erased_lifetime(erased_lifetime&&) = delete;
  erased_lifetime& operator=(erased_lifetime&&) = delete;

  ~erased_lifetime() { reset(); }

  template <class T, class... Args>
  T& emplace(Args&&... args) {
    static_assert(sizeof(T) <= Size,
                  "object is too large for erased_lifetime storage");
    static_assert(alignof(T) <= Align,
                  "object alignment exceeds erased_lifetime storage");

    reset();
    T* object =
        ::new (static_cast<void*>(storage_)) T(std::forward<Args>(args)...);
    destroy_ = &destroy_model<T>;
    return *object;
  }

  void reset() noexcept {
    if (destroy_ == nullptr) {
      return;
    }

    auto destroy = std::exchange(destroy_, nullptr);
    destroy(static_cast<void*>(storage_));
  }

  [[nodiscard]] bool has_value() const noexcept { return destroy_ != nullptr; }

 private:
  template <class T>
  static void destroy_model(void* storage) noexcept {
    std::launder(reinterpret_cast<T*>(storage))->~T();
  }

  alignas(Align) std::byte storage_[Size]{};
  void (*destroy_)(void*) noexcept {nullptr};
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_ERASED_LIFETIME_HPP_
