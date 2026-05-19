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
    completion_adaptors
    coroutine_task
    environment
    into_variant
    just_then
    let
    repeat_until
    run_loop
    scheduler
    scheduling_adaptors
    split_with_let
    sync_wait
    when_all
)

foreach(example IN LISTS BEXEC_EXAMPLES)
    add_executable(bexec_example_${example} ${example}.cpp)
    target_link_libraries(bexec_example_${example} PRIVATE bexec::bexec)
endforeach()
