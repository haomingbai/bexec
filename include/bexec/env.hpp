#pragma once

#include <bexec/query.hpp>
#include <bexec/stop_token.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace bexec {

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

    template <class QueryTag, class... Args>
        requires(!std::same_as<std::remove_cvref_t<QueryTag>, get_stop_token_t> &&
                 requires(const BaseEnv& base, QueryTag&& tag, Args&&... args) {
                     bexec::query(base, std::forward<QueryTag>(tag),
                                  std::forward<Args>(args)...);
                 })
    decltype(auto) query(QueryTag&& tag, Args&&... args) const
        noexcept(noexcept(bexec::query(base_, std::forward<QueryTag>(tag),
                                       std::forward<Args>(args)...))) {
        return bexec::query(base_, std::forward<QueryTag>(tag),
                            std::forward<Args>(args)...);
    }

private:
    inplace_stop_token token_;
    BaseEnv base_;
};

} // namespace bexec
