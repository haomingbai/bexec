/**
 * @file include/bexec/bexec.hpp
 * @brief Umbrella header for the bexec sender/receiver library.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-05-12
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 *
 * @details
 * Includes the public vocabulary, factories, adaptors, algorithms, stop-token
 * utilities, coroutine task, and io_context scheduler headers.
 */

#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_BEXEC_HPP_
#define BEXEC_INCLUDE_BEXEC_BEXEC_HPP_

#include <bexec/completion_signatures.hpp>
#include <bexec/concepts.hpp>
#include <bexec/cpo.hpp>
#include <bexec/env.hpp>
#include <bexec/io_context/io_context.hpp>
#include <bexec/just.hpp>
#include <bexec/let.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/query.hpp>
#include <bexec/receiver.hpp>
#include <bexec/repeat_until.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <bexec/stop_token.hpp>
#include <bexec/task.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>
#endif  // BEXEC_INCLUDE_BEXEC_BEXEC_HPP_
