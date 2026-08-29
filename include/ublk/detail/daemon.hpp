/**
 * @file daemon.hpp
 * @brief Implementation of the ublk daemon API for managing ublk device
 * servers.
 */

#pragma once

#include "ublk/detail/control.hpp"
#include "ublk/detail/io.hpp"
#include "ublk/detail/io_loop.hpp"
#include "ublk/detail/shm.hpp"
#include "ublk/detail/utils.hpp"
#include "ublk/handler.hpp"
#include "ublk/runtime.hpp"
#include "ublk/ublk_cmd.h"
#include <condy.hpp>
#include <stdexcept>

namespace ublk {
namespace detail {

namespace ex = condy::detail::ex;

inline bool need_recovery(const ublksrv_ctrl_dev_info *info) noexcept {
    return info->state == UBLK_S_DEV_QUIESCED ||
           info->state == UBLK_S_DEV_FAIL_IO;
}

struct daemon_setup_t {
    template <typename Sched, typename Alloc>
    ex::task<bool, TaskEnv<Sched, Alloc>> invoke(int control_fd,
                                                 ublksrv_ctrl_dev_info *info) {
        if (info->dev_id == -1) {
            co_await raw::add_dev(control_fd, info);
            co_return true;
        }

        auto s = raw::add_dev(control_fd, info) |
                 ex::upon_error([&](std::error_code ec) {
                     if (ec.value() != EEXIST) {
                         throw std::system_error(ec, "add_dev");
                     }
                     return -ec.value();
                 });
        auto r = co_await std::move(s);
        if (r != -EEXIST) {
            co_return true;
        }

        co_await control_get_dev_info_t{}.invoke<Sched, Alloc>(
            control_fd, info->dev_id, info);

        if (info->flags & UBLK_F_USER_RECOVERY && need_recovery(info)) {
            co_await (control_start_user_recovery_t{}.invoke<Sched, Alloc>(
                          control_fd, info->dev_id) |
                      ex::write_env(ex::prop{fetch_dev_info, info}));
        }

        co_return false;
    }
};

struct daemon_configure_t {
    template <typename Sched, typename Alloc>
    ex::task<bool, TaskEnv<Sched, Alloc>>
    invoke(int control_fd, uint32_t dev_id, ublk_params *params) {
        ublksrv_ctrl_dev_info info;
        co_await cached_get_dev_info<Sched, Alloc>(control_fd, dev_id, &info);

        if (info.state == UBLK_S_DEV_DEAD) {
            co_await (control_set_params_t{}.invoke<Sched, Alloc>(
                          control_fd, dev_id, params) |
                      ex::write_env(ex::prop{fetch_dev_info, &info}));
            co_return true;
        } else {
            co_await (control_get_params_t{}.invoke<Sched, Alloc>(
                          control_fd, dev_id, params) |
                      ex::write_env(ex::prop{fetch_dev_info, &info}));
            co_return false;
        }
    }
};

struct daemon_start_t {
    template <typename Sched, typename Alloc>
    ex::task<void, TaskEnv<Sched, Alloc>>
    invoke(int control_fd, uint32_t dev_id, int32_t daemon_pid) {
        ublksrv_ctrl_dev_info info;
        co_await cached_get_dev_info<Sched, Alloc>(control_fd, dev_id, &info);

        if (!need_recovery(&info)) {
            co_await (control_start_dev_t{}.invoke<Sched, Alloc>(
                          control_fd, dev_id, daemon_pid) |
                      ex::write_env(ex::prop{fetch_dev_info, &info}));
        } else if (info.flags & UBLK_F_USER_RECOVERY) {
            co_await (control_end_user_recovery_t{}.invoke<Sched, Alloc>(
                          control_fd, dev_id, daemon_pid) |
                      ex::write_env(ex::prop{fetch_dev_info, &info}));
        } else {
            throw std::runtime_error(
                "device can neither be started nor recovered");
        }
    }
};

struct daemon_run_t {
    template <typename Sched, typename Alloc, IoHandler Handler>
    ex::task<void, TaskEnv<Sched, Alloc>>
    invoke(int control_fd, uint32_t dev_id, Handler *handler,
           const RuntimeOptions *runtime_options, size_t nr_files,
           size_t nr_buffers) {
        auto alloc = co_await ex::read_env(ex::get_allocator);

        ublksrv_ctrl_dev_info info;
        co_await cached_get_dev_info<Sched, Alloc>(control_fd, dev_id, &info);

        nr_files = std::max<size_t>(nr_files, 1);
        if (info.flags & (UBLK_F_SUPPORT_ZERO_COPY | UBLK_F_AUTO_BUF_REG)) {
            nr_buffers = std::max<size_t>(nr_buffers, info.queue_depth);
        }
        auto options = runtime_options
                           ? runtime_options->build(info.queue_depth)
                           : condy::RuntimeOptions();

        AllocVector<std::unique_ptr<IoLoop>, decltype(alloc)> loops(alloc);
        loops.reserve(info.nr_hw_queues);
        for (uint16_t q_id = 0; q_id < info.nr_hw_queues; q_id++) {
            cpu_set_t cpuset;
            co_await (control_get_queue_affinity_t{}.invoke<Sched, Alloc>(
                          control_fd, dev_id, q_id, &cpuset) |
                      ex::write_env(ex::prop{fetch_dev_info, &info}));
            auto loop =
                std::make_unique<IoLoop>(options, nr_files, nr_buffers, cpuset);
            loops.push_back(std::move(loop));
            options.enable_attach_wq(loops[0]->runtime());
        }

        std::string path = dev_path(dev_id);
        int ublkc_fd = co_await condy::async_open(path.c_str(), O_RDWR, 0);
        auto d = defer([&] noexcept { close(ublkc_fd); });

        ex::simple_counting_scope scope;
        AllocVector<std::exception_ptr, decltype(alloc)> errs(info.nr_hw_queues,
                                                              alloc);
        for (uint16_t q_id = 0; q_id < info.nr_hw_queues; q_id++) {
            auto sched = loops[q_id]->get_scheduler();
            auto env = ex::env{ex::prop{ex::get_allocator, alloc},
                               ex::prop{fetch_dev_info, &info}};
            auto task = io_run_dev_t{}.invoke<decltype(sched), Alloc>(
                            ublkc_fd, q_id, &info, handler) |
                        ex::write_env(std::move(env));
            auto s = ex::starts_on(sched, std::move(task)) |
                     ex::upon_error(
                         [&, q_id](const std::exception_ptr &ep) noexcept {
                             errs[q_id] = ep;
                         });
            ex::spawn(std::move(s), scope.get_token());
        }
        co_await scope.join();

        for (auto &ep : errs) {
            if (ep) {
                std::rethrow_exception(ep);
            }
        }
    }
};

struct daemon_shm_server_t {
    template <typename Sched, typename Alloc, ShmHandler Handler>
    auto invoke(int control_fd, uint32_t dev_id, std::string_view path,
                Handler *handler, uint32_t flags) {
        return shm_server_run<Sched, Alloc>(
            path, Session<Sched, Alloc, Handler>(control_fd, dev_id, flags,
                                                 *handler));
    }
};

} // namespace detail
} // namespace ublk