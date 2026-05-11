#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <memory>
#include <stdexcept>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {

void test_then() {
  {
    auto state = std::make_shared<shared_state>();
    bool called = false;
    auto sender = bexec::just(5) | bexec::then([&](int) { called = true; });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(called);
    CHECK(state->signal == signal_kind::value);
  }

  {
    auto state = std::make_shared<shared_state>();
    auto sender = bexec::just() | bexec::then([] {
                    throw std::runtime_error("boom");
                    return 1;
                  });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});

    bexec::start(operation);
    CHECK(state->signal == signal_kind::error);
    CHECK(static_cast<bool>(state->exception));
  }
}

}  // namespace bexec_tests
