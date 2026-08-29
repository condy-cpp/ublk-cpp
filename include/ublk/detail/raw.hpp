/**
 * @file raw.hpp
 * @brief Low-level io_uring command senders for ublk control and I/O
 * commands.
 */

#pragma once

#include "ublk/ublk_cmd.h"
#include <condy.hpp>
#include <cstdint>

namespace ublk {
namespace detail {

template <typename Fd>
inline auto ctrl_cmd(Fd fd, int cmd_op, uint32_t dev_id, uint16_t queue_id,
                     uint64_t data0, uint64_t addr, uint16_t len,
                     uint16_t dev_path_len) noexcept {
    auto prep = [=](io_uring_sqe *sqe) noexcept {
        auto &cmd = reinterpret_cast<ublksrv_ctrl_cmd &>(sqe->cmd);
        cmd = {};
        cmd.dev_id = dev_id;
        cmd.queue_id = queue_id;
        cmd.data[0] = data0;
        cmd.addr = addr;
        cmd.len = len;
        cmd.dev_path_len = dev_path_len;
    };
#ifdef UBLKCPP_USE_URING_CMD128
    return condy::async_uring_cmd128(cmd_op, fd, prep,
                                     condy::SimpleCQEHandler{});
#else
    return condy::async_uring_cmd(cmd_op, fd, prep, condy::SimpleCQEHandler{});
#endif
}

template <typename Fd>
inline auto io_cmd(Fd fd, int cmd_op, uint16_t q_id, uint16_t tag,
                   int32_t result, uint64_t addr, uint64_t sqe_addr) noexcept {
    return condy::async_uring_cmd(
        cmd_op, fd,
        [=](io_uring_sqe *sqe) noexcept {
            auto &cmd = reinterpret_cast<ublksrv_io_cmd &>(sqe->cmd);
            cmd = {};
            cmd.q_id = q_id;
            cmd.tag = tag;
            cmd.result = result;
            cmd.addr = addr;
            sqe->addr = sqe_addr;
        },
        condy::SimpleCQEHandler{});
}

template <typename Fd>
inline auto batch_io_cmd(Fd fd, int cmd_op, uint16_t q_id, uint16_t flags,
                         uint16_t nr_elem, uint8_t elem_bytes, uint64_t addr,
                         uint32_t len) noexcept {
    return condy::async_uring_cmd(
        cmd_op, fd,
        [=](io_uring_sqe *sqe) noexcept {
            auto &cmd = reinterpret_cast<ublk_batch_io &>(sqe->cmd);
            cmd = {};
            cmd.q_id = q_id;
            cmd.flags = flags;
            cmd.nr_elem = nr_elem;
            cmd.elem_bytes = elem_bytes;
            sqe->addr = addr;
            sqe->len = len;
        },
        condy::SimpleCQEHandler{});
}

} // namespace detail
} // namespace ublk