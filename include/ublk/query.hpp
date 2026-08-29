/**
 * @file query.hpp
 * @brief Environment queries used by the library's senders.
 */

#pragma once

#include "ublk/ublk_cmd.h"
#include <condy.hpp>
#include <utility>

namespace ublk {

namespace detail {
namespace ex = condy::detail::ex;
}

/**
 * @brief Environment query for the cached ublk device info. Returns nullptr if
 * the environment doesn't provide it.
 */
struct fetch_dev_info_t {
    template <typename Env>
        requires requires(const Env &env) {
            env.query(std::declval<fetch_dev_info_t>());
        }
    constexpr const ublksrv_ctrl_dev_info *
    operator()(const Env &env) const noexcept {
        return env.query(fetch_dev_info_t{});
    }

    template <typename Env>
        requires(!requires(const Env &env) {
            env.query(std::declval<fetch_dev_info_t>());
        })
    constexpr const ublksrv_ctrl_dev_info *
    operator()(const Env &) const noexcept {
        return nullptr;
    }

    [[nodiscard]]
    static consteval bool query(detail::ex::forwarding_query_t) noexcept {
        return true;
    }
};

/** @brief Query object instance of fetch_dev_info_t. */
inline constexpr fetch_dev_info_t fetch_dev_info{};

} // namespace ublk