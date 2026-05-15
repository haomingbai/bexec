# @file examples/CMakeLists.txt
# @brief CMake target definitions for bexec examples.
# @author Haoming Bai <haomingbai@hotmail.com>
# @date   2026-05-12
#
# Copyright © 2026 Haoming Bai
# SPDX-License-Identifier: MIT
#
# @details
# Builds one executable per example case and links each target to the bexec
# interface library.

set(
    BEXEC_EXAMPLES
    coroutine_task
    environment
    just_then
    let
    repeat_until
    scheduler
    when_all
)

foreach(example IN LISTS BEXEC_EXAMPLES)
    add_executable(bexec_example_${example} ${example}.cpp)
    target_link_libraries(bexec_example_${example} PRIVATE bexec::bexec)
endforeach()
