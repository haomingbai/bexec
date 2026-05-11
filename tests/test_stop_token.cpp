#include <bexec/stop_token.hpp>

#include "test_support.hpp"

namespace bexec_tests {

void test_stop_token() {
  bexec::inplace_stop_source source;
  auto token = source.get_token();

  int callbacks = 0;
  {
    bexec::inplace_stop_callback callback{token, [&] { ++callbacks; }};
    CHECK(!token.stop_requested());
    CHECK(source.request_stop());
    CHECK(callbacks == 1);
  }

  bexec::inplace_stop_callback immediate{token, [&] { ++callbacks; }};
  CHECK(callbacks == 2);
  CHECK(token.stop_requested());
}

}  // namespace bexec_tests
