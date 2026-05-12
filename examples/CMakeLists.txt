# @file examples/CMakeLists.txt
# @brief CMake target definition for bexec examples.
# @author Haoming Bai <haomingbai@hotmail.com>
# @date   2026-05-12
#
# Copyright © 2026 Haoming Bai
# SPDX-License-Identifier: MIT
#
# @details
# Builds the example executable and links it to the bexec interface library.

add_executable(bexec_examples basic.cpp)
target_link_libraries(bexec_examples PRIVATE bexec::bexec)
