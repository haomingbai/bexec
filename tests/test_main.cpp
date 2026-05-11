#include "test_support.hpp"

#include <iostream>

namespace bexec_tests {
int failures = 0;
}

int main() {
    bexec_tests::test_concepts();
    bexec_tests::test_just();
    bexec_tests::test_then();
    bexec_tests::test_stop_token();
    bexec_tests::test_env();
    bexec_tests::test_scheduler();
    bexec_tests::test_repeat_until();
    bexec_tests::test_when_all();
    bexec_tests::test_task();

    if (bexec_tests::failures != 0) {
        std::cerr << bexec_tests::failures << " test failure(s)\n";
        return 1;
    }

    std::cout << "all tests passed\n";
    return 0;
}
