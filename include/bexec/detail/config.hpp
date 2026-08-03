/**
 * @file include/bexec/detail/config.hpp
 * @brief Internal compile-time configuration macros.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Provides the internal unreachable helper. Exception handling in sender
 * implementations is selected purely by noexcept analysis of the invoked
 * callable (see bexec/detail/type_traits.hpp); the library does not track
 * whether exceptions are enabled at compile time. Building with exceptions
 * disabled is the user's responsibility and is not guaranteed to compile.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_CONFIG_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_CONFIG_HPP_

#if defined(__clang__) || defined(__GNUC__)
#define BEXEC_DETAIL_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
#define BEXEC_DETAIL_UNREACHABLE() __assume(false)
#else
#define BEXEC_DETAIL_UNREACHABLE() \
  do {                             \
    for (;;) {                     \
    }                              \
  } while (false)
#endif
#endif  // BEXEC_INCLUDE_BEXEC_DETAIL_CONFIG_HPP_
