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

set(BEXEC_VERSION "0.0.1" CACHE STRING "bexec package version")

project(
    bexec
    VERSION "${BEXEC_VERSION}"
    DESCRIPTION "A small C++20 sender/receiver concurrency library"
    LANGUAGES CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(BEXEC_IS_TOP_LEVEL ON)
else()
    set(BEXEC_IS_TOP_LEVEL OFF)
endif()

add_library(bexec INTERFACE)
add_library(bexec::bexec ALIAS bexec)

target_compile_features(bexec INTERFACE cxx_std_20)
target_include_directories(
    bexec
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

option(BEXEC_BUILD_TESTS "Build bexec tests" ${BEXEC_IS_TOP_LEVEL})
option(BEXEC_BUILD_EXAMPLES "Build bexec examples" ${BEXEC_IS_TOP_LEVEL})

if(BEXEC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BEXEC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

set(
    BEXEC_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/bexec"
    CACHE STRING
    "Directory in which to install the bexec CMake package"
)

install(
    TARGETS bexec
    EXPORT bexecTargets
)
install(
    DIRECTORY include/bexec
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
    FILES LICENSE README.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(
    EXPORT bexecTargets
    FILE bexecTargets.cmake
    NAMESPACE bexec::
    DESTINATION "${BEXEC_INSTALL_CMAKEDIR}"
)

configure_package_config_file(
    cmake/bexecConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/bexecConfig.cmake"
    INSTALL_DESTINATION "${BEXEC_INSTALL_CMAKEDIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/bexecConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/bexecConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/bexecConfigVersion.cmake"
    DESTINATION "${BEXEC_INSTALL_CMAKEDIR}"
)

if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    file(
        RELATIVE_PATH BEXEC_PC_PREFIX_RELATIVE
        "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
        "${CMAKE_INSTALL_PREFIX}"
    )
else()
    file(
        RELATIVE_PATH BEXEC_PC_PREFIX_RELATIVE
        "/${CMAKE_INSTALL_LIBDIR}/pkgconfig"
        "/"
    )
endif()
configure_file(cmake/bexec.pc.in "${CMAKE_CURRENT_BINARY_DIR}/bexec.pc" @ONLY)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/bexec.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)

if(BEXEC_IS_TOP_LEVEL)
    set(CPACK_PACKAGE_NAME "bexec")
    set(CPACK_PACKAGE_VENDOR "Haoming Bai")
    set(CPACK_PACKAGE_CONTACT "Haoming Bai <haomingbai@hotmail.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/haomingbai/bexec")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "all")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "libdevel")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

    set(CPACK_RPM_PACKAGE_ARCHITECTURE "noarch")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")
    set(CPACK_RPM_PACKAGE_RELEASE 1)
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

    set(CPACK_GENERATOR "DEB;RPM")
    include(CPack)
endif()
