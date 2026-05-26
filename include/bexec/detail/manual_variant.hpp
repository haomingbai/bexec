/**
 * @file include/bexec/detail/manual_variant.hpp
 * @brief Internal storage for one of several explicitly managed object types.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-27
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides variant-like storage that can construct non-movable alternatives
 * directly from a factory result.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_MANUAL_VARIANT_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_MANUAL_VARIANT_HPP_

#include <bexec/completion_signatures.hpp>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace bexec::detail {

template <std::size_t First, std::size_t... Rest>
struct static_max
    : std::integral_constant<std::size_t, ((First > static_max<Rest...>::value)
                                               ? First
                                               : static_max<Rest...>::value)> {
};

template <std::size_t Value>
struct static_max<Value> : std::integral_constant<std::size_t, Value> {};

template <class Alternatives>
class manual_variant;

template <class... Alternatives>
class manual_variant<type_list<Alternatives...>> {
 public:
  manual_variant() noexcept = default;

  manual_variant(const manual_variant&) = delete;
  manual_variant& operator=(const manual_variant&) = delete;
  manual_variant(manual_variant&&) = delete;
  manual_variant& operator=(manual_variant&&) = delete;

  ~manual_variant() { reset(); }

  template <class T, class... Args>
  T& emplace(Args&&... args) {
    static_assert(type_list_contains_v<T, type_list<Alternatives...>>,
                  "type is not listed in this manual_variant");

    reset();
    T* object =
        ::new (static_cast<void*>(storage_)) T(std::forward<Args>(args)...);
    destroy_ = &destroy_model<T>;
    return *object;
  }

  template <class T, class Factory>
  T& emplace_from(Factory&& factory) {
    static_assert(type_list_contains_v<T, type_list<Alternatives...>>,
                  "type is not listed in this manual_variant");

    reset();
    T* object = ::new (static_cast<void*>(storage_))
        T(std::forward<Factory>(factory)());
    destroy_ = &destroy_model<T>;
    return *object;
  }

  void reset() noexcept {
    if (destroy_ == nullptr) {
      return;
    }

    destroy_(static_cast<void*>(storage_));
    destroy_ = nullptr;
  }

  [[nodiscard]] bool has_value() const noexcept { return destroy_ != nullptr; }

  template <class T>
  T& get() noexcept {
    static_assert(type_list_contains_v<T, type_list<Alternatives...>>,
                  "type is not listed in this manual_variant");
    return *std::launder(reinterpret_cast<T*>(static_cast<void*>(storage_)));
  }

  template <class T>
  const T& get() const noexcept {
    static_assert(type_list_contains_v<T, type_list<Alternatives...>>,
                  "type is not listed in this manual_variant");
    return *std::launder(
        reinterpret_cast<const T*>(static_cast<const void*>(storage_)));
  }

 private:
  template <class T>
  static void destroy_model(void* storage) noexcept {
    std::launder(reinterpret_cast<T*>(storage))->~T();
  }

  static constexpr std::size_t storage_size =
      static_max<1U, sizeof(Alternatives)...>::value;
  static constexpr std::size_t storage_align =
      static_max<1U, alignof(Alternatives)...>::value;

  alignas(storage_align) unsigned char storage_[storage_size];
  void (*destroy_)(void*) noexcept {nullptr};
};

}  // namespace bexec::detail
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_MANUAL_VARIANT_HPP_
