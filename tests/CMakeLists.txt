# @file tests/CMakeLists.txt
# @brief CMake target definition for bexec tests.
# @author Haoming Bai <haomingbai@hotmail.com>
# @date   2026-05-12
#
# Copyright © 2026 Haoming Bai
# SPDX-License-Identifier: MIT
#
# @details
# Finds an installed GoogleTest package or downloads a pinned release as a
# fallback, then builds one executable per feature. Each executable contains
# basic, integration, and stress cases for that feature and is registered three
# times with CTest so tests can be selected independently by feature or
# category.

if(NOT TARGET GTest::gtest_main)
    find_package(GTest QUIET)
endif()

if(NOT TARGET GTest::gtest_main)
    include(FetchContent)

    # Keep the fallback dependency limited to the test framework actually used
    # by bexec. Normal (non-cache) variables avoid changing parent-project
    # options when bexec itself is included with add_subdirectory().
    set(BUILD_GMOCK OFF)
    set(INSTALL_GTEST OFF)
    set(gtest_build_samples OFF)
    set(gtest_build_tests OFF)
    set(gtest_force_shared_crt ON)

    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
        URL_HASH
            SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    )
    FetchContent_MakeAvailable(googletest)
endif()

set(BEXEC_TEST_TARGETS)

function(bexec_add_feature_test feature)
    set(target "bexec_test_${feature}")
    add_executable(
        "${target}"
        test_support.cpp
        ${ARGN}
    )
    target_link_libraries(
        "${target}"
        PRIVATE
            bexec::bexec
            GTest::gtest_main
    )
    target_include_directories("${target}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND BEXEC_TEST_TARGETS "${target}")
    set(BEXEC_TEST_TARGETS "${BEXEC_TEST_TARGETS}" PARENT_SCOPE)

    foreach(category IN ITEMS basic integration stress)
        set(test_name "bexec.${feature}.${category}")
        add_test(
            NAME "${test_name}"
            COMMAND "${target}" "--gtest_filter=${category}.*"
        )
        set_tests_properties(
            "${test_name}"
            PROPERTIES
                LABELS "${feature};${category}"
                TIMEOUT 30
        )
    endforeach()
endfunction()

bexec_add_feature_test(
    vocabulary
    basic/completion_signatures.cpp
    basic/concepts.cpp
    basic/env.cpp
    integration/vocabulary.cpp
    stress/vocabulary.cpp
)
bexec_add_feature_test(
    just
    basic/just.cpp
    integration/just.cpp
    stress/just.cpp
)
bexec_add_feature_test(
    then
    basic/then.cpp
    integration/then.cpp
    stress/then.cpp
)
bexec_add_feature_test(
    let
    basic/let.cpp
    integration/let.cpp
    stress/let.cpp
)
bexec_add_feature_test(
    into_variant
    basic/into_variant.cpp
    integration/into_variant.cpp
    stress/into_variant.cpp
)
bexec_add_feature_test(
    stop_token
    basic/stop_token.cpp
    integration/stop_token.cpp
    stress/stop_token_races.cpp
)
bexec_add_feature_test(
    run_loop
    basic/run_loop.cpp
    integration/run_loop.cpp
    stress/run_loop.cpp
)
bexec_add_feature_test(
    on
    basic/on.cpp
    integration/on.cpp
    stress/on.cpp
)
bexec_add_feature_test(
    sync_wait
    basic/sync_wait.cpp
    integration/sync_wait.cpp
    stress/sync_wait.cpp
)
bexec_add_feature_test(
    repeat_until
    basic/repeat_until.cpp
    integration/repeat_until.cpp
    stress/repeat_until.cpp
)
bexec_add_feature_test(
    when_all
    basic/when_all.cpp
    integration/when_all.cpp
    stress/when_all.cpp
)
bexec_add_feature_test(
    counting_scope
    basic/counting_scope.cpp
    integration/counting_scope.cpp
    stress/counting_scope.cpp
)
bexec_add_feature_test(
    task
    basic/task.cpp
    integration/task.cpp
    stress/task.cpp
)
bexec_add_feature_test(
    generator
    basic/generator.cpp
    integration/generator.cpp
    stress/generator.cpp
)

file(GLOB_RECURSE BEXEC_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/bexec/*.hpp"
)
set(BEXEC_HEADER_CHECK_SOURCES)
foreach(header IN LISTS BEXEC_HEADERS)
    file(RELATIVE_PATH relative_header "${PROJECT_SOURCE_DIR}/include" "${header}")
    string(MAKE_C_IDENTIFIER "${relative_header}" source_id)
    set(source "${CMAKE_CURRENT_BINARY_DIR}/header_${source_id}.cpp")
    file(GENERATE
        OUTPUT "${source}"
        CONTENT "#include <${relative_header}>\n"
    )
    list(APPEND BEXEC_HEADER_CHECK_SOURCES "${source}")
endforeach()

add_library(bexec_header_self_contained OBJECT ${BEXEC_HEADER_CHECK_SOURCES})
target_link_libraries(bexec_header_self_contained PRIVATE bexec::bexec)

add_custom_target(
    bexec_tests
    DEPENDS ${BEXEC_TEST_TARGETS} bexec_header_self_contained
)
