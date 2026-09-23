# @file benchmarks/CMakeLists.txt
# @brief CMake target definitions for the per-feature bexec benchmarks.
# @author Haoming Bai <haomingbai@hotmail.com>
# @date   2026-09-23
#
# Copyright © 2026 Haoming Bai
# SPDX-License-Identifier: MIT
#
# @details
# Builds one benchmark executable per feature. Every benchmark case comes in
# two flavors, `.bexec` and `.stdexec`, running the same logic against bexec
# and against the stdexec reference implementation for comparison.
#
# stdexec is fetched with FetchContent below, which only executes when the
# bexec benchmarks are built (BEXEC_BUILD_BENCHMARKS=ON).
#
# Each executable is registered with CTest in smoke mode (--smoke bounds the
# per-case work so the run only proves the benchmarks still execute). The full
# timings come from running the executables without --smoke in a Release
# build:
#
#   cmake --build build --target bexec_benchmarks
#   ./build/benchmarks/bexec_bench_just
#
# All executables can be smoke-run through CTest with:
#
#   ctest --test-dir build -L benchmark --output-on-failure

include(FetchContent)

set(STDEXEC_BUILD_TESTS OFF)
set(STDEXEC_INSTALL OFF)

FetchContent_Declare(
    stdexec
    URL https://github.com/NVIDIA/stdexec/archive/refs/tags/nvhpc-26.05.tar.gz
    URL_HASH
        SHA256=9d2396fecd604698c1eae58f0cb6e4517aa727013846240d1a7b2f35e49884dc
)
FetchContent_MakeAvailable(stdexec)

set(BEXEC_BENCH_TARGETS)

function(bexec_add_feature_benchmark feature)
    set(target "bexec_bench_${feature}")
    add_executable(
        "${target}"
        ${feature}.cpp
    )
    target_link_libraries(
        "${target}"
        PRIVATE
            bexec::bexec
            STDEXEC::stdexec
    )
    target_include_directories("${target}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND BEXEC_BENCH_TARGETS "${target}")
    set(BEXEC_BENCH_TARGETS "${BEXEC_BENCH_TARGETS}" PARENT_SCOPE)

    set(test_name "bexec.${feature}.benchmark")
    add_test(
        NAME "${test_name}"
        COMMAND "${target}" "--smoke"
    )
    set_tests_properties(
        "${test_name}"
        PROPERTIES
            LABELS "${feature};benchmark"
            TIMEOUT 300
    )
endfunction()

bexec_add_feature_benchmark(vocabulary)
bexec_add_feature_benchmark(just)
bexec_add_feature_benchmark(then)
bexec_add_feature_benchmark(let)
bexec_add_feature_benchmark(into_variant)
bexec_add_feature_benchmark(repeat_until)
bexec_add_feature_benchmark(run_loop)
bexec_add_feature_benchmark(on)
bexec_add_feature_benchmark(sync_wait)
bexec_add_feature_benchmark(when_all)
bexec_add_feature_benchmark(task)
bexec_add_feature_benchmark(counting_scope)
bexec_add_feature_benchmark(stop_token)

add_custom_target(
    bexec_benchmarks
    DEPENDS ${BEXEC_BENCH_TARGETS}
)
