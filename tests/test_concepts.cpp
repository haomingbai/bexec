#include "test_support.hpp"

#include <bexec/concepts.hpp>
#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>

#include <memory>
#include <utility>

namespace bexec_tests {

void test_concepts() {
    static_assert(bexec::sender<decltype(bexec::just(1))>);

    auto state = std::make_shared<shared_state>();
    any_receiver receiver{state};
    auto sender = bexec::just(1) | bexec::then([](int value) { return value + 1; });
    auto operation = bexec::connect(std::move(sender), receiver);
    static_assert(bexec::operation_state<decltype(operation)>);

    bexec::start(operation);
    CHECK(state->signal == signal_kind::value);
    CHECK(state->int_value == 2);
}

} // namespace bexec_tests
