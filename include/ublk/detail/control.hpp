/**
 * @file control.hpp
 * @brief Implementation of ublk control command interface
 */

#pragma once

#include "ublk/detail/path.hpp"
#include "ublk/detail/task.hpp"
#include "ublk/raw.hpp"
#include <condy.hpp>

namespace ublk {
namespace detail {

namespace ex = condy::detail::ex;

struct control_get_dev_info_t {
    template <typename Sched, typename Alloc>
    ex::task<void, TaskEnv<Sched, Alloc>> invoke(int fd, uint32_t dev_id,
                                                 ublksrv_ctrl_dev_info *info) {
        DevPathBuf<ublksrv_ctrl_dev_info> buf(dev_id);
        auto s =
            raw::get_dev_info2(fd, dev_id, &buf, sizeof(buf), DEV_PATH_LEN) |
            ex::then([&](int32_t r) noexcept {
                *info = buf.payload;
                return r;
            }) |
            ex::let_error([&](std::error_code ec) {
                if (ec.value() != EOPNOTSUPP) {
                    throw std::system_error(ec, "get_dev_info2");
                }
                return raw::get_dev_info(fd, dev_id, info);
            });
        co_await std::move(s);
    }
};

template <typename Sched, typename Alloc>
inline ex::task<void, TaskEnv<Sched, Alloc>>
cached_get_dev_info(int fd, uint32_t dev_id, ublksrv_ctrl_dev_info *info) {
    const auto *dev_info = co_await ex::read_env(fetch_dev_info);
    if (dev_info && dev_info->dev_id == dev_id) {
        *info = *dev_info;
        co_return;
    }
    co_await control_get_dev_info_t{}.invoke<Sched, Alloc>(fd, dev_id, info);
}

struct control_get_queue_affinity_t {
    template <typename Sched, typename Alloc>
    ex::task<void, TaskEnv<Sched, Alloc>>
    invoke(int control_fd, uint32_t dev_id, uint16_t q_id, cpu_set_t *cpuset) {
        ublksrv_ctrl_dev_info info;
        co_await cached_get_dev_info<Sched, Alloc>(control_fd, dev_id, &info);
        if (info.flags & UBLK_F_UNPRIVILEGED_DEV) {
            DevPathBuf<cpu_set_t> buf(dev_id);
            co_await raw::get_queue_affinity(control_fd, dev_id, q_id, &buf,
                                             sizeof(buf), DEV_PATH_LEN);
            *cpuset = buf.payload;
        } else {
            co_await raw::get_queue_affinity(control_fd, dev_id, q_id, cpuset);
        }
    }
};

template <typename Sched, typename Alloc, typename Fn>
inline ex::task<void, TaskEnv<Sched, Alloc>>
with_dev_path(int control_fd, uint32_t dev_id, Fn fn) {
    ublksrv_ctrl_dev_info info;
    char path[DEV_PATH_LEN];
    fill_dev_path(path, sizeof(path), dev_id);
    auto s = cached_get_dev_info<Sched, Alloc>(control_fd, dev_id, &info) |
             ex::let_value([&, fn = std::move(fn)]() noexcept(
                               noexcept(std::move(fn)(path, DEV_PATH_LEN))) {
                 if (info.flags & UBLK_F_UNPRIVILEGED_DEV) {
                     return std::move(fn)(path, DEV_PATH_LEN);
                 } else {
                     return std::move(fn)(nullptr, 0);
                 }
             });
    co_await std::move(s);
}

struct control_del_dev_t {
    template <typename Sched, typename Alloc>
    auto invoke(int control_fd, uint32_t dev_id) {
        return with_dev_path<Sched, Alloc>(
            control_fd, dev_id, [=](char *path, uint16_t len) noexcept {
                return raw::del_dev(control_fd, dev_id, path, len);
            });
    }
};

struct control_start_dev_t {
    template <typename Sched, typename Alloc>
    auto invoke(int control_fd, uint32_t dev_id, int32_t daemon_pid) {
        return with_dev_path<Sched, Alloc>(
            control_fd, dev_id, [=](char *path, uint16_t len) noexcept {
                return raw::start_dev(control_fd, dev_id, daemon_pid, path,
                                      len);
            });
    }
};

struct control_stop_dev_t {
    template <typename Sched, typename Alloc>
    auto invoke(int control_fd, uint32_t dev_id) {
        return with_dev_path<Sched, Alloc>(
            control_fd, dev_id, [=](char *path, uint16_t len) noexcept {
                return raw::stop_dev(control_fd, dev_id, path, len);
            });
    }
};

struct control_set_params_t {
    template <typename Sched, typename Alloc>
    ex::task<void, TaskEnv<Sched, Alloc>>
    invoke(int control_fd, uint32_t dev_id, const ublk_params *params) {
        ublksrv_ctrl_dev_info info;
        co_await cached_get_dev_info<Sched, Alloc>(control_fd, dev_id, &info);
        ublk_params full = *params;
        full.len = sizeof(full);
        if (info.flags & UBLK_F_UNPRIVILEGED_DEV) {
            DevPathBuf<ublk_params> buf(dev_id);
            buf.payload = full;
            co_await raw::set_params(control_fd, dev_id, &buf, sizeof(buf),
                                     DEV_PATH_LEN);
        } else {
            co_await raw::set_params(control_fd, dev_id, &full);
        }
    }
};

struct control_get_params_t {
    template <typename Sched, typename Alloc>
    ex::task<void, TaskEnv<Sched, Alloc>>
    invoke(int control_fd, uint32_t dev_id, ublk_params *params) {
        ublksrv_ctrl_dev_info info;
        co_await cached_get_dev_info<Sched, Alloc>(control_fd, dev_id, &info);
        params->len = sizeof(*params);
        if (info.flags & UBLK_F_UNPRIVILEGED_DEV) {
            DevPathBuf<ublk_params> buf(dev_id);
            buf.payload.len = sizeof(buf.payload);
            co_await raw::get_params(control_fd, dev_id, &buf, sizeof(buf),
                                     DEV_PATH_LEN);
            *params = buf.payload;
        } else {
            co_await raw::get_params(control_fd, dev_id, params);
        }
    }
};

struct control_start_user_recovery_t {
    template <typename Sched, typename Alloc>
    auto invoke(int control_fd, uint32_t dev_id) {
        return with_dev_path<Sched, Alloc>(
            control_fd, dev_id, [=](char *path, uint16_t len) noexcept {
                return raw::start_user_recovery(control_fd, dev_id, path, len);
            });
    }
};

struct control_end_user_recovery_t {
    template <typename Sched, typename Alloc>
    auto invoke(int control_fd, uint32_t dev_id, int32_t daemon_pid) {
        return with_dev_path<Sched, Alloc>(
            control_fd, dev_id, [=](char *path, uint16_t len) noexcept {
                return raw::end_user_recovery(control_fd, dev_id, daemon_pid,
                                              path, len);
            });
    }
};

struct control_del_dev_async_t {
    template <typename Sched, typename Alloc>
    auto invoke(int control_fd, uint32_t dev_id) {
        return with_dev_path<Sched, Alloc>(
            control_fd, dev_id, [=](char *path, uint16_t len) noexcept {
                return raw::del_dev_async(control_fd, dev_id, path, len);
            });
    }
};

struct control_update_size_t {
    template <typename Sched, typename Alloc>
    auto invoke(int control_fd, uint32_t dev_id, uint64_t sectors) {
        return with_dev_path<Sched, Alloc>(
            control_fd, dev_id, [=](char *path, uint16_t len) noexcept {
                return raw::update_size(control_fd, dev_id, sectors, path, len);
            });
    }
};

struct control_quiesce_dev_t {
    template <typename Sched, typename Alloc>
    auto invoke(int control_fd, uint32_t dev_id, uint64_t timeout_ms) {
        return with_dev_path<Sched, Alloc>(
            control_fd, dev_id, [=](char *path, uint16_t len) noexcept {
                return raw::quiesce_dev(control_fd, dev_id, timeout_ms, path,
                                        len);
            });
    }
};

struct control_try_stop_dev_t {
    template <typename Sched, typename Alloc>
    auto invoke(int control_fd, uint32_t dev_id) {
        return with_dev_path<Sched, Alloc>(
            control_fd, dev_id, [=](char *path, uint16_t len) noexcept {
                return raw::try_stop_dev(control_fd, dev_id, path, len);
            });
    }
};

struct control_register_shm_buf_t {
    template <typename Sched, typename Alloc>
    ex::task<int32_t, TaskEnv<Sched, Alloc>>
    invoke(int control_fd, uint32_t dev_id, const ublk_shmem_buf_reg *reg) {
        ublksrv_ctrl_dev_info info;
        co_await cached_get_dev_info<Sched, Alloc>(control_fd, dev_id, &info);
        if (info.flags & UBLK_F_UNPRIVILEGED_DEV) {
            DevPathBuf<ublk_shmem_buf_reg> buf(dev_id);
            buf.payload = *reg;
            co_return co_await raw::register_shm_buf(control_fd, dev_id, &buf,
                                                     sizeof(buf), DEV_PATH_LEN);
        } else {
            co_return co_await raw::register_shm_buf(control_fd, dev_id, reg);
        }
    }
};

struct control_unregister_shm_buf_t {
    template <typename Sched, typename Alloc>
    auto invoke(int control_fd, uint32_t dev_id, uint64_t buf_index) {
        return with_dev_path<Sched, Alloc>(
            control_fd, dev_id, [=](char *path, uint16_t len) noexcept {
                return raw::unregister_shm_buf(control_fd, dev_id, buf_index,
                                               path, len);
            });
    }
};

} // namespace detail
} // namespace ublk