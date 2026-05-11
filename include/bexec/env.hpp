#pragma once

#include <bexec/stop_token.hpp>

#include <concepts>
#include <utility>

namespace bexec {

/**
 * @brief Query tag used to obtain a stop token from an environment.
 */
struct get_stop_token_t {};

/**
 * @brief Query tag used to obtain a scheduler from an environment.
 */
struct get_scheduler_t {};

inline constexpr get_stop_token_t get_stop_token{};
inline constexpr get_scheduler_t get_scheduler{};

/**
 * @brief Member-based query CPO.
 *
 * query(env, tag) calls env.query(tag). No tag_invoke fallback is provided.
 */
struct query_t {
    template <class Env, class QueryTag>
        requires requires(Env&& env, QueryTag tag) {
            std::forward<Env>(env).query(tag);
        }
    constexpr decltype(auto) operator()(Env&& env, QueryTag tag) const
        noexcept(noexcept(std::forward<Env>(env).query(tag))) {
        return std::forward<Env>(env).query(tag);
    }
};

inline constexpr query_t query{};

/**
 * @brief Empty environment that provides a never_stop_token.
 */
struct empty_env {
    /** @brief Returns a token that never requests stop. */
    [[nodiscard]] never_stop_token query(get_stop_token_t) const noexcept {
        return {};
    }
};

/**
 * @brief Environment wrapper that overrides get_stop_token and delegates other queries.
 */
template <class BaseEnv = empty_env>
class env_with_stop_token {
public:
    env_with_stop_token(inplace_stop_token token, BaseEnv base = {})
        : token_(std::move(token)), base_(std::move(base)) {}

    [[nodiscard]] inplace_stop_token query(get_stop_token_t) const noexcept {
        return token_;
    }

    template <class QueryTag>
        requires(!std::same_as<QueryTag, get_stop_token_t> &&
                 requires(const BaseEnv& base, QueryTag tag) { bexec::query(base, tag); })
    decltype(auto) query(QueryTag tag) const
        noexcept(noexcept(bexec::query(base_, tag))) {
        return bexec::query(base_, tag);
    }

private:
    inplace_stop_token token_;
    BaseEnv base_;
};

/**
 * @brief Receiver environment CPO.
 *
 * get_env(receiver) calls receiver.get_env() when available, otherwise returns
 * empty_env.
 */
struct get_env_t {
    template <class Receiver>
    constexpr auto operator()(Receiver&& receiver) const {
        if constexpr (requires { std::forward<Receiver>(receiver).get_env(); }) {
            return std::forward<Receiver>(receiver).get_env();
        } else {
            return empty_env{};
        }
    }
};

inline constexpr get_env_t get_env{};

} // namespace bexec
