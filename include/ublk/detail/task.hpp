/**
 * @file task.hpp
 * @brief Task environment utils.
 */

#pragma once

#include "ublk/query.hpp"
#include "ublk/ublk_cmd.h"
#include <condy.hpp>

namespace ublk {
namespace detail {

namespace ex = condy::detail::ex;

template <typename Sched, typename Alloc> struct TaskEnv {
    using start_scheduler_type = Sched;
    using allocator_type = Alloc;

    const ublksrv_ctrl_dev_info *info = nullptr;

    template <typename> using env_type = TaskEnv;

    template <typename Env>
        requires requires(const Env &env) { env.query(fetch_dev_info_t{}); }
    constexpr TaskEnv(const Env &env) noexcept
        : info(env.query(fetch_dev_info_t{})) {}

    constexpr TaskEnv() noexcept = default;

    [[nodiscard]]
    constexpr auto query(fetch_dev_info_t) const noexcept {
        return info;
    }
};

struct get_allocator_with_default_t {
    template <typename Env>
        requires requires(Env const &env) { ex::get_allocator(env); }
    constexpr auto operator()(Env const &env) const noexcept {
        return ex::get_allocator(env);
    }

    template <typename Env>
        requires(!requires(Env const &env) { ex::get_allocator(env); })
    constexpr std::allocator<std::byte> operator()(Env const &) const noexcept {
        return {};
    }
};

inline constexpr get_allocator_with_default_t get_allocator_with_default{};

template <typename TaskFn, typename... Args>
auto task_invoke(TaskFn &&task_fn, Args &&...args) noexcept {
    return ex::read_env(ex::get_start_scheduler) |
           ex::let_value([task_fn = std::forward<TaskFn>(task_fn),
                          ... args = std::forward<Args>(args)](
                             const auto &sched) noexcept {
               using Sched = std::decay_t<decltype(sched)>;
               return ex::read_env(get_allocator_with_default) |
                      ex::let_value([task_fn = std::move(task_fn),
                                     ... args = std::move(args)](
                                        const auto &alloc) mutable {
                          using Alloc = std::decay_t<decltype(alloc)>;
                          return std::move(task_fn)
                              .template invoke<Sched, Alloc>(
                                  std::move(args)...);
                      });
           });
}

} // namespace detail
} // namespace ublk