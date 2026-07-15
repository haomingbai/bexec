cmake_minimum_required(VERSION 3.20)

project(bexec_pkgconfig_consumer LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(BEXEC REQUIRED IMPORTED_TARGET bexec>=0.0.1)

add_executable(bexec_pkgconfig_consumer ../consumer.cpp)
target_link_libraries(bexec_pkgconfig_consumer PRIVATE PkgConfig::BEXEC)
target_compile_features(bexec_pkgconfig_consumer PRIVATE cxx_std_20)

enable_testing()
add_test(NAME bexec_pkgconfig_consumer COMMAND bexec_pkgconfig_consumer)
