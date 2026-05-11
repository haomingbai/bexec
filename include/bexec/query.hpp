#pragma once

#ifndef BEXEC_INCLUDE_BEXEC_QUERY_HPP_
#define BEXEC_INCLUDE_BEXEC_QUERY_HPP_

#include <bexec/stop_token.hpp>

#include <utility>

namespace bexec {

/**
 * @brief Query object used to obtain a stop token from a queryable object.
 */
struct get_stop_token_t {
    template <class Env>
    constexpr auto operator()(Env&& env) const noexcept {
        if constexpr (requires { std::as_const(env).query(*this); }) {
            return std::as_const(env).query(*this);
        } else {
            return never_stop_token{};
        }
    }
};

/**
 * @brief Query object used to obtain a scheduler from a queryable object.
 */
struct get_scheduler_t {
    template <class Env>
        requires requires(Env&& env, const get_scheduler_t& self) {
            std::as_const(env).query(self);
        }
    constexpr decltype(auto) operator()(Env&& env) const noexcept {
        return std::as_const(env).query(*this);
    }
};

inline constexpr get_stop_token_t get_stop_token{};
inline constexpr get_scheduler_t get_scheduler{};

/**
 * @brief Invokes a query object with a queryable object.
 */
struct query_t {
    template <class Env, class QueryTag, class... Args>
        requires requires(QueryTag&& tag, Env&& env, Args&&... args) {
            std::forward<QueryTag>(tag)(std::forward<Env>(env),
                                        std::forward<Args>(args)...);
        }
    constexpr decltype(auto) operator()(Env&& env, QueryTag&& tag, Args&&... args) const
        noexcept(noexcept(std::forward<QueryTag>(tag)(std::forward<Env>(env),
                                                      std::forward<Args>(args)...))) {
        return std::forward<QueryTag>(tag)(std::forward<Env>(env),
                                           std::forward<Args>(args)...);
    }
};

inline constexpr query_t query{};

} // namespace bexec
#endif  // BEXEC_INCLUDE_BEXEC_QUERY_HPP_
