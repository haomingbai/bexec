# @file CMakeLists.txt
# @brief Top-level CMake build definition for bexec.
# @author Haoming Bai <haomingbai@hotmail.com>
# @date   2026-05-12
#
# Copyright © 2026 Haoming Bai
# SPDX-License-Identifier: MIT
#
# @details
# Declares the header-only interface target, public include directories, C++20
# requirement, and optional tests and examples.

cmake_minimum_required(VERSION 3.20)

project(
    bexec
    VERSION 0.1.0
    DESCRIPTION "A small C++20 sender/receiver concurrency library"
    LANGUAGES CXX
)

add_library(bexec INTERFACE)
add_library(bexec::bexec ALIAS bexec)

target_compile_features(bexec INTERFACE cxx_std_20)
target_include_directories(
    bexec
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

option(BEXEC_BUILD_TESTS "Build bexec tests" ON)
option(BEXEC_BUILD_EXAMPLES "Build bexec examples" ON)

if(BEXEC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BEXEC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
