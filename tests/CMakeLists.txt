add_executable(
    bexec_tests
    test_main.cpp
    test_concepts.cpp
    test_env.cpp
    test_just.cpp
    test_repeat_until.cpp
    test_scheduler.cpp
    test_stop_token.cpp
    test_task.cpp
    test_then.cpp
    test_when_all.cpp
)
target_link_libraries(bexec_tests PRIVATE bexec::bexec)

add_test(NAME bexec_tests COMMAND bexec_tests)
