/**
 * @file tests/stress/vocabulary.cpp
 * @brief Environment-query stress tests.
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

TEST(stress, vocabulary_query_stability) {
  bexec::run_loop loop;
  bexec::inplace_stop_source source;
  bexec::env_with_scheduler scheduler_env{loop.get_scheduler(),
                                          bexec::empty_env{}};
  bexec::env_with_stop_token env{source.get_token(), scheduler_env};

  const int iterations = stress_iterations(100000);
  for (int index = 0; index != iterations; ++index) {
    EXPECT_EQ(bexec::query(env, bexec::get_scheduler),
              loop.get_scheduler());
    EXPECT_FALSE(bexec::query(env, bexec::get_stop_token).stop_requested());
  }
}

}  // namespace bexec_tests
