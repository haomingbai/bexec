#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_SCHEDULER_HPP_
#define BEXEC_INCLUDE_BEXEC_SCHEDULER_HPP_

#include <bexec/sender.hpp>
#include <concepts>
#include <utility>

namespace bexec {

/**
 * @brief Obtains a scheduling sender by calling scheduler.schedule().
 */
struct schedule_t {
  template <class Scheduler>
    requires requires(Scheduler&& scheduler) {
      std::forward<Scheduler>(scheduler).schedule();
    }
  constexpr decltype(auto) operator()(Scheduler&& scheduler) const
      noexcept(noexcept(std::forward<Scheduler>(scheduler).schedule())) {
    return std::forward<Scheduler>(scheduler).schedule();
  }
};

inline constexpr schedule_t schedule{};

/**
 * @brief Concept for scheduler-like types that provide schedule().
 */
template <class Scheduler>
concept scheduler = std::copy_constructible<std::remove_cvref_t<Scheduler>> &&
                    std::equality_comparable<std::remove_cvref_t<Scheduler>> &&
                    requires(Scheduler&& sched) {
                      {
                        bexec::schedule(std::forward<Scheduler>(sched))
                      } -> sender;
                    };

}  // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_SCHEDULER_HPP_
