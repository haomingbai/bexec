#include <bexec/bexec.hpp>

int main() {
  auto result = bexec::this_thread::sync_wait(
      bexec::just(41) | bexec::then([](int value) { return value + 1; }));
  return result && std::get<0>(*result) == 42 ? 0 : 1;
}
