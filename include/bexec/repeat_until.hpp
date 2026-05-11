#pragma once

#include <bexec/completion_signatures.hpp>
#include <bexec/detail/repeat_until.hpp>
#include <bexec/detail/type_traits.hpp>

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace bexec {

/**
 * @brief Sender that repeats a sender-producing callable until a predicate succeeds.
 *
 * Child sender values are discarded. This design intentionally uses a factory
 * callable so every iteration receives a fresh sender and operation state.
 */
template <class Factory, class Predicate>
class repeat_until_sender {
public:
    using sender_type = std::invoke_result_t<Factory&>;
    using completion_signatures = bexec::completion_signatures<
        type_list<value_signature<>>,
        detail::sender_errors_with_exception_t<sender_type>,
        true>;

    repeat_until_sender(Factory factory, Predicate predicate)
        : factory_(std::move(factory)), predicate_(std::move(predicate)) {}

    template <class Receiver>
    auto connect(Receiver receiver) && {
        return detail::repeat_until_operation<Factory, Predicate, Receiver>{
            std::move(factory_), std::move(predicate_), std::move(receiver)};
    }

    template <class Receiver>
        requires std::copy_constructible<Factory> && std::copy_constructible<Predicate>
    auto connect(Receiver receiver) const& {
        return detail::repeat_until_operation<Factory, Predicate, Receiver>{
            factory_, predicate_, std::move(receiver)};
    }

private:
    Factory factory_;
    Predicate predicate_;
};

/**
 * @brief Repeats factory() until predicate() returns true.
 */
template <class Factory, class Predicate>
[[nodiscard]] auto repeat_until(Factory&& factory, Predicate&& predicate) {
    return repeat_until_sender<std::decay_t<Factory>, std::decay_t<Predicate>>{
        std::forward<Factory>(factory), std::forward<Predicate>(predicate)};
}

} // namespace bexec
