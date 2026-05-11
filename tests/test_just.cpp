#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <memory>
#include <string>

#include "test_support.hpp"

namespace bexec_tests {

void test_just() {
  {
    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(bexec::just(std::make_unique<int>(42)),
                                    any_receiver{state});

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

}  // namespace bexec_tests
