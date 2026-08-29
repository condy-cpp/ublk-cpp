/**
 * @file io.hpp
 * @brief Implementation of the ublk I/O loop for processing I/O requests on a
 * dedicated scheduler.
 */

#pragma once

#include "ublk/detail/queue.hpp"
#include "ublk/detail/task.hpp"
#include "ublk/handler.hpp"
#include <condy.hpp>
#include <cstdint>
#include <stdexcept>

namespace ublk {
namespace detail {

namespace ex = condy::detail::ex;

struct io_run_dev_t {
    template <typename Sched, typename Alloc, IoHandler Handler>
    ex::task<void, TaskEnv<Sched, Alloc>>
    invoke(int ublkc_fd, uint16_t q_id, const ublksrv_ctrl_dev_info *info,
           Handler *handler) {
        if (q_id >= info->nr_hw_queues) {
            throw std::out_of_range("q_id out of range");
        }

        uint64_t flags = info->flags;
        uint16_t queue_depth = info->queue_depth;
        uint32_t max_io_buf_bytes = info->max_io_buf_bytes;

        if (flags & (UBLK_F_SUPPORT_ZERO_COPY | UBLK_F_AUTO_BUF_REG)) {
            iovec iov = {nullptr, 0};
            auto &buffer_table = condy::current_runtime().buffer_table();
            if (auto r = buffer_table.update(queue_depth - 1, &iov, 1); r < 0) {
                throw std::runtime_error(
                    "Buffer table too small, need nr_buffers >= queue_depth");
            }
        }

        constexpr int UBLKC_FD = 0;
        auto &fd_table = condy::current_runtime().fd_table();
        auto r = fd_table.update(UBLKC_FD, &ublkc_fd, 1);
        auto d = defer([&] noexcept {
            if (r >= 0) {
                int fd = -1;
                fd_table.update(UBLKC_FD, &fd, 1); // unregister
            }
        });

        auto alloc = co_await ex::read_env(ex::get_allocator);
        if (flags & UBLK_F_BATCH_IO) {
            if (r < 0) {
                BatchIoQueue queue(ublkc_fd, ublkc_fd, q_id, flags, queue_depth,
                                   max_io_buf_bytes, *handler, alloc);
                co_await queue.template run<Sched>();
            } else {
                BatchIoQueue queue(condy::fixed(UBLKC_FD), ublkc_fd, q_id,
                                   flags, queue_depth, max_io_buf_bytes,
                                   *handler, alloc);
                co_await queue.template run<Sched>();
            }
        } else {
            if (r < 0) {
                IoQueue queue(ublkc_fd, ublkc_fd, q_id, flags, queue_depth,
                              max_io_buf_bytes, *handler, alloc);
                co_await queue.template run<Sched>();
            } else {
                IoQueue queue(condy::fixed(UBLKC_FD), ublkc_fd, q_id, flags,
                              queue_depth, max_io_buf_bytes, *handler, alloc);
                co_await queue.template run<Sched>();
            }
        }
    }
};

} // namespace detail
} // namespace ublk