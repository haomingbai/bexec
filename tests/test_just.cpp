#include "test_support.hpp"

#include <bexec/cpo.hpp>
#include <bexec/just.hpp>

#include <memory>
#include <string>

namespace bexec_tests {

void test_just() {
    {
        auto state = std::make_shared<shared_state>();
        auto operation =
            bexec::connect(bexec::just(std::make_unique<int>(42)), any_receiver{state});

        bexec::start(operation);
        CHECK(state->signal == signal_kind::value);
        CHECK(state->int_value == 42);
    }

    {
        auto state = std::make_shared<shared_state>();
        auto operation = bexec::connect(bexec::just_error(std::string{"failed"}),
                                        any_receiver{state});

        bexec::start(operation);
        CHECK(state->signal == signal_kind::error);
    }

    {
        auto state = std::make_shared<shared_state>();
        auto operation = bexec::connect(bexec::just_stopped(), any_receiver{state});

        bexec::start(operation);
        CHECK(state->signal == signal_kind::stopped);
    }
}

} // namespace bexec_tests
