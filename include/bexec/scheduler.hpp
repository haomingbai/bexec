#pragma once

#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>

namespace bexec {

namespace detail {
class schedule_sender;
}

/**
 * @brief A simple thread-safe FIFO event loop.
 *
 * run() drains currently queued work until the queue is empty or stop() is
 * requested. enqueue/post are thread-safe; handlers execute on the thread that
 * calls run() or run_one().
 */
class io_context {
public:
    class scheduler;

    io_context() = default;
    io_context(const io_context&) = delete;
    io_context& operator=(const io_context&) = delete;

    /** @brief Returns a scheduler associated with this event loop. */
    [[nodiscard]] scheduler get_scheduler() noexcept;

    /** @brief Enqueues work. Returns false when the context is stopped. */
    bool post(std::function<void()> work) {
        {
            std::lock_guard lock(mutex_);
            if (stopped_) {
                return false;
            }
            queue_.push_back(std::move(work));
        }
        cv_.notify_one();
        return true;
    }

    /** @brief Alias for post(). */
    bool enqueue(std::function<void()> work) {
        return post(std::move(work));
    }

    /** @brief Runs one queued item if available. */
    std::size_t run_one() {
        std::function<void()> work;
        {
            std::lock_guard lock(mutex_);
            if (stopped_ || queue_.empty()) {
                return 0;
            }
            work = std::move(queue_.front());
            queue_.pop_front();
        }
        work();
        return 1;
    }

    /** @brief Drains queued work until empty or stopped. */
    std::size_t run() {
        std::size_t count = 0;
        while (run_one() != 0) {
            ++count;
        }
        return count;
    }

    /** @brief Requests that run()/run_one() stop processing new work. */
    void stop() noexcept {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    /** @brief Clears the stopped flag so new work can be enqueued. */
    void restart() noexcept {
        std::lock_guard lock(mutex_);
        stopped_ = false;
    }

    /** @brief Returns true if stop() has been called and restart() has not. */
    [[nodiscard]] bool stopped() const noexcept {
        std::lock_guard lock(mutex_);
        return stopped_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
    bool stopped_{false};
};

/**
 * @brief Scheduler handle for io_context.
 */
class io_context::scheduler {
public:
    scheduler() = default;

    /** @brief Returns a sender that completes on the associated io_context. */
    [[nodiscard]] detail::schedule_sender schedule() const;

    /** @brief Enqueues arbitrary work on the associated io_context. */
    bool post(std::function<void()> work) const {
        return context_->post(std::move(work));
    }

    /** @brief Awaiter that resumes a coroutine on the associated io_context. */
    class schedule_awaiter {
    public:
        explicit schedule_awaiter(scheduler sched)
            : context_(sched.context_) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) const {
            if (!context_->post([handle]() mutable { handle.resume(); })) {
                handle.resume();
            }
        }

        void await_resume() const noexcept {}

    private:
        io_context* context_;
    };

    /** @brief Returns an awaitable that resumes on this scheduler. */
    [[nodiscard]] schedule_awaiter schedule_awaitable() const {
        return schedule_awaiter{*this};
    }

    friend bool operator==(scheduler lhs, scheduler rhs) noexcept {
        return lhs.context_ == rhs.context_;
    }

private:
    friend class io_context;

    explicit scheduler(io_context& context)
        : context_(&context) {}

    io_context* context_{nullptr};
};

inline io_context::scheduler io_context::get_scheduler() noexcept {
    return scheduler{*this};
}

} // namespace bexec

#include <bexec/detail/schedule_sender.hpp>

namespace bexec {

inline detail::schedule_sender io_context::scheduler::schedule() const {
    return detail::schedule_sender{*context_};
}

} // namespace bexec
