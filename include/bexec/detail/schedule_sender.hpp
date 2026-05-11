#pragma once

#include <bexec/completion_signatures.hpp>
#include <bexec/cpo.hpp>
#include <bexec/detail/config.hpp>
#include <bexec/env.hpp>

#include <exception>
#include <memory>
#include <optional>
#include <utility>

namespace bexec {
class io_context;
}

namespace bexec::detail {

class schedule_sender {
public:
    using completion_signatures =
        bexec::completion_signatures<type_list<value_signature<>>,
                                      type_list<std::exception_ptr>,
                                      true>;

    explicit schedule_sender(io_context& context)
        : context_(&context) {}

    template <class Receiver>
    class operation {
    public:
        operation(io_context& context, Receiver receiver)
            : context_(&context), receiver_(std::move(receiver)) {}

        void start() {
            auto token = bexec::query(bexec::get_env(*receiver_), bexec::get_stop_token);
            if (token.stop_requested()) {
                bexec::set_stopped(std::move(*receiver_));
                receiver_.reset();
                return;
            }

#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
            try {
#endif
                auto receiver = std::make_shared<Receiver>(std::move(*receiver_));
                receiver_.reset();

                const bool queued = context_->post([receiver]() mutable {
                    auto inner_token =
                        bexec::query(bexec::get_env(*receiver), bexec::get_stop_token);
                    if (inner_token.stop_requested()) {
                        bexec::set_stopped(std::move(*receiver));
                    } else {
                        bexec::set_value(std::move(*receiver));
                    }
                });

                if (!queued) {
                    bexec::set_stopped(std::move(*receiver));
                }
#if BEXEC_DETAIL_EXCEPTIONS_ENABLED
            } catch (...) {
                bexec::set_error(std::move(*receiver_), std::current_exception());
                receiver_.reset();
            }
#endif
        }

    private:
        io_context* context_;
        std::optional<Receiver> receiver_;
    };

    template <class Receiver>
    auto connect(Receiver receiver) const {
        return operation<Receiver>{*context_, std::move(receiver)};
    }

private:
    io_context* context_;
};

} // namespace bexec::detail
