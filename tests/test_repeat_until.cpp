#include <bexec/io_context/io_context.hpp>
#include <bexec/just.hpp>
#include <bexec/operation_state.hpp>
#include <bexec/repeat_until.hpp>
#include <bexec/scheduler.hpp>
#include <bexec/sender.hpp>
#include <bexec/then.hpp>
#include <memory>
#include <string>
#include <utility>

#include "test_support.hpp"

namespace bexec_tests {

void test_repeat_until() {
  {
    int count = 0;
    auto sender = bexec::repeat_until(
        [&] { return bexec::just() | bexec::then([&] { ++count; }); },
        [&] { return count == 3; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(count == 3);
    CHECK(state->signal == signal_kind::value);
  }

  {
    int count = 0;
    auto sender = bexec::repeat_until(
        [&] { return bexec::just() | bexec::then([&] { ++count; }); },
        [&] { return count == 10000; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(count == 10000);
    CHECK(state->signal == signal_kind::value);
  }

  {
    bexec::io_context context;
    int count = 0;
    auto sender = bexec::repeat_until(
        [&] {
          return bexec::schedule(context.get_scheduler()) |
                 bexec::then([&] { ++count; });
        },
        [&] { return count == 5; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(context.run() == 5);
    CHECK(count == 5);
    CHECK(state->signal == signal_kind::value);
  }

  {
    auto sender = bexec::repeat_until(
        [] { return bexec::just_error(std::string{"bad"}); },
        [] { return false; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(state->signal == signal_kind::error);
  }

  {
    auto sender = bexec::repeat_until([] { return bexec::just_stopped(); },
                                      [] { return false; });

    auto state = std::make_shared<shared_state>();
    auto operation = bexec::connect(std::move(sender), any_receiver{state});
    bexec::start(operation);

    CHECK(state->signal == signal_kind::stopped);
  }
}

}  // namespace bexec_tests
