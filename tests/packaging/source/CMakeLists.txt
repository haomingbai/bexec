cmake_minimum_required(VERSION 3.20)

project(bexec_source_consumer LANGUAGES CXX)

if(NOT BEXEC_SOURCE_DIR)
    message(FATAL_ERROR "BEXEC_SOURCE_DIR must point to the bexec source tree")
endif()

add_subdirectory("${BEXEC_SOURCE_DIR}" bexec)

add_executable(bexec_source_consumer ../consumer.cpp)
target_link_libraries(bexec_source_consumer PRIVATE bexec::bexec)

enable_testing()
add_test(NAME bexec_source_consumer COMMAND bexec_source_consumer)
