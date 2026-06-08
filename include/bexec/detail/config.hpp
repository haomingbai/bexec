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
 * Detects whether C++ exceptions are enabled and exposes the
 * BEXEC_DETAIL_EXCEPTIONS_ENABLED switch used by exception-sensitive sender
 * implementations.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_DETAIL_CONFIG_HPP_
#define BEXEC_INCLUDE_BEXEC_DETAIL_CONFIG_HPP_

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define BEXEC_DETAIL_EXCEPTIONS_ENABLED 1
#else
#define BEXEC_DETAIL_EXCEPTIONS_ENABLED 0
#endif

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
