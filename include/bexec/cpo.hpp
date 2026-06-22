/**
 * @file include/bexec/cpo.hpp
 * @brief Aggregate header for bexec customization point objects.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Re-exports the member-based customization point objects for starting
 * operations, connecting senders, delivering receiver completions,
 * scheduling, querying environments, and adapting senders to awaitables.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_CPO_HPP_
#define BEXEC_INCLUDE_BEXEC_CPO_HPP_

#include <bexec/awaitable.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#endif  // BEXEC_INCLUDE_BEXEC_CPO_HPP_
