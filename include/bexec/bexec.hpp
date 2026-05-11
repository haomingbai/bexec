#pragma once

/**
 * @file bexec.hpp
 * @brief Umbrella header for the bexec sender/receiver library.
 *
 * bexec is intentionally member-customization based. It does not use
 * tag_invoke and it does not depend on stdexec.
 */

#include <bexec/completion_signatures.hpp>
#include <bexec/concepts.hpp>
#include <bexec/cpo.hpp>
#include <bexec/env.hpp>
#include <bexec/just.hpp>
#include <bexec/repeat_until.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/stop_token.hpp>
#include <bexec/task.hpp>
#include <bexec/then.hpp>
#include <bexec/when_all.hpp>
