cmake_minimum_required(VERSION 3.20)

project(bexec_cmake_consumer LANGUAGES CXX)

find_package(bexec 0.0.1 CONFIG REQUIRED)

add_executable(bexec_cmake_consumer ../consumer.cpp)
target_link_libraries(bexec_cmake_consumer PRIVATE bexec::bexec)

enable_testing()
add_test(NAME bexec_cmake_consumer COMMAND bexec_cmake_consumer)
