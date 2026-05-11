#pragma once

#include <bexec/cpo.hpp>

#include <utility>

namespace bexec::detail {

template <class Operation>
class pass_through_operation {
public:
    explicit pass_through_operation(Operation operation)
        : operation_(std::move(operation)) {}

    void start() {
        bexec::start(operation_);
    }

private:
    Operation operation_;
};

} // namespace bexec::detail
