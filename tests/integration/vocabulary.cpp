/**
 * @file tests/integration/vocabulary.cpp
 * @brief Layered environment-query integration tests.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-06-18
 *
 * Copyright © 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include <bexec/env.hpp>
#include <bexec/query.hpp>
#include <bexec/run_loop.hpp>

#include "test_support.hpp"

namespace bexec_tests {

BEXEC_TEST_CASE(vocabulary_layered_environment_queries, integration) {
  bexec::run_loop loop;
  bexec::inplace_stop_source source;
  bexec::env_with_scheduler scheduler_env{loop.get_scheduler(),
                                          bexec::empty_env{}};
  bexec::env_with_stop_token env{source.get_token(), scheduler_env};

  CHECK(bexec::get_scheduler(env) == loop.get_scheduler());
  CHECK(bexec::get_delegation_scheduler(env) == loop.get_scheduler());
  CHECK(!bexec::get_stop_token(env).stop_requested());
  CHECK(source.request_stop());
  CHECK(bexec::get_stop_token(env).stop_requested());
}

}  // namespace bexec_tests
