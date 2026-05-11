add_executable(bexec_tests test_bexec.cpp)
target_link_libraries(bexec_tests PRIVATE bexec::bexec)

add_test(NAME bexec_tests COMMAND bexec_tests)
