/**
 * @file io.hpp
 * @brief Public API for running I/O on a single ublk queue.
 */

#pragma once

#include "ublk/detail/io.hpp"
#include "ublk/detail/task.hpp"

namespace ublk {

/**
 * @brief Run the I/O loop of a single ublk queue until it stops.
 * @param ublkc_fd The file descriptor of the ublk queue (/dev/ublkcN).
 * @param q_id The queue ID of the queue to run.
 * @param info A pointer to the device information of the ublk device.
 * @param handler The IoHandler invoked for each fetched request.
 * @return A sender completing when the queue stops, or with an error on
 *         failure.
 */
template <IoHandler F>
inline auto run_dev(int ublkc_fd, uint16_t q_id,
                    const ublksrv_ctrl_dev_info *info, F &handler) noexcept {
    return detail::task_invoke(detail::io_run_dev_t{}, ublkc_fd, q_id, info,
                               &handler);
}

} // namespace ublk