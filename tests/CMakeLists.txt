# @file tests/CMakeLists.txt
# @brief CMake target definition for bexec tests.
# @author Haoming Bai <haomingbai@hotmail.com>
# @date   2026-05-12
#
# Copyright © 2026 Haoming Bai
# SPDX-License-Identifier: MIT
#
# @details
# Builds the single bexec_tests executable from all test modules, links it to
# the interface library, and registers it with CTest.

add_executable(
    bexec_tests
    test_main.cpp
    test_completion_signatures.cpp
    test_concepts.cpp
    test_env.cpp
    test_just.cpp
    test_let.cpp
    test_repeat_until.cpp
    test_scheduler.cpp
    test_stop_token.cpp
    test_task.cpp
    test_then.cpp
    test_when_all.cpp
)
target_link_libraries(bexec_tests PRIVATE bexec::bexec)

add_test(NAME bexec_tests COMMAND bexec_tests)
