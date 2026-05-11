#include "test_support.hpp"

#include <bexec/cpo.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/then.hpp>

#include <memory>
#include <utility>

namespace bexec_tests {

void test_scheduler() {
    bexec::io_context context;
    bool ran = false;
    auto state = std::make_shared<shared_state>();

    auto sender = bexec::schedule(context.get_scheduler()) | bexec::then([&] { ran = true; });
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(!ran);
    CHECK(state->signal == signal_kind::none);
    CHECK(context.run() == 1);
    CHECK(ran);
    CHECK(state->signal == signal_kind::value);
}

} // namespace bexec_tests
