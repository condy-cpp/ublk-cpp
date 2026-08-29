/**
 * @file raw.hpp
 * @brief Low-level io_uring command senders for ublk control and I/O
 * commands.
 */

#pragma once

#include "ublk/detail/raw.hpp"

#ifndef IORING_URING_CMD_MULTISHOT
#define IORING_URING_CMD_MULTISHOT (1U << 1)
#endif

namespace ublk {
/**
 * @brief Low-level io_uring command senders for ublk control and I/O
 *        commands.
 */
namespace raw {

/**
 * @brief Unprivileged version of get_queue_affinity().
 */
template <typename Fd>
inline auto get_queue_affinity(Fd fd, uint32_t dev_id, uint16_t q_id,
                               void *data, uint16_t size,
                               uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(
        fd, static_cast<int>(UBLK_U_CMD_GET_QUEUE_AFFINITY), dev_id, q_id, q_id,
        reinterpret_cast<uint64_t>(data), size, dev_path_len);
}

/**
 * @brief Get the queue affinity for a specific ublk queue.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param q_id The queue ID for which to retrieve the affinity information.
 * @param cpuset A pointer to the CPU set to store the affinity information.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto get_queue_affinity(Fd fd, uint32_t dev_id, uint16_t q_id,
                               cpu_set_t *cpuset) noexcept {
    return get_queue_affinity(fd, dev_id, q_id, cpuset, sizeof(*cpuset), 0);
}

/**
 * @brief Unprivileged version of get_dev_info().
 */
template <typename Fd>
inline auto get_dev_info(Fd fd, uint32_t dev_id, void *data, uint16_t size,
                         uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_GET_DEV_INFO),
                            dev_id, -1, 0, reinterpret_cast<uint64_t>(data),
                            size, dev_path_len);
}

/**
 * @brief Get the device information for a specific ublk device.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param info A pointer to the device information structure to populate.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto get_dev_info(Fd fd, uint32_t dev_id,
                         ublksrv_ctrl_dev_info *info) noexcept {
    return get_dev_info(fd, dev_id, info, sizeof(*info), 0);
}

/**
 * @brief Add a new ublk device.
 * @param fd The control file descriptor for the ublk device.
 * @param info A pointer to the device information structure describing the
 *             device to add.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto add_dev(Fd fd, ublksrv_ctrl_dev_info *info) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_ADD_DEV),
                            info->dev_id, -1, 0,
                            reinterpret_cast<uint64_t>(info), sizeof(*info), 0);
}

/**
 * @brief Unprivileged version of del_dev().
 */
template <typename Fd>
inline auto del_dev(Fd fd, uint32_t dev_id, const char *dev_path,
                    uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_DEL_DEV), dev_id,
                            -1, 0, reinterpret_cast<uint64_t>(dev_path),
                            dev_path_len, dev_path_len);
}

/**
 * @brief Delete a ublk device.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device to delete.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd> inline auto del_dev(Fd fd, uint32_t dev_id) noexcept {
    return del_dev(fd, dev_id, nullptr, 0);
}

/**
 * @brief Unprivileged version of start_dev().
 */
template <typename Fd>
inline auto start_dev(Fd fd, uint32_t dev_id, int32_t daemon_pid,
                      const char *dev_path, uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_START_DEV), dev_id,
                            -1, static_cast<uint64_t>(daemon_pid),
                            reinterpret_cast<uint64_t>(dev_path), dev_path_len,
                            dev_path_len);
}

/**
 * @brief Start a ublk device.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param daemon_pid The PID of the daemon process that serves the device.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto start_dev(Fd fd, uint32_t dev_id, int32_t daemon_pid) noexcept {
    return start_dev(fd, dev_id, daemon_pid, nullptr, 0);
}

/**
 * @brief Unprivileged version of stop_dev().
 */
template <typename Fd>
inline auto stop_dev(Fd fd, uint32_t dev_id, const char *dev_path,
                     uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_STOP_DEV), dev_id,
                            -1, 0, reinterpret_cast<uint64_t>(dev_path),
                            dev_path_len, dev_path_len);
}

/**
 * @brief Stop a ublk device.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device to stop.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd> inline auto stop_dev(Fd fd, uint32_t dev_id) noexcept {
    return stop_dev(fd, dev_id, nullptr, 0);
}

/**
 * @brief Unprivileged version of set_params().
 */
template <typename Fd>
inline auto set_params(Fd fd, uint32_t dev_id, const void *data, uint16_t size,
                       uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_SET_PARAMS), dev_id,
                            -1, 0, reinterpret_cast<uint64_t>(data), size,
                            dev_path_len);
}

/**
 * @brief Set the parameters of a ublk device.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param params A pointer to the parameters to apply to the device.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto set_params(Fd fd, uint32_t dev_id,
                       const ublk_params *params) noexcept {
    return set_params(fd, dev_id, params, sizeof(*params), 0);
}

/**
 * @brief Unprivileged version of get_params().
 */
template <typename Fd>
inline auto get_params(Fd fd, uint32_t dev_id, void *data, uint16_t size,
                       uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_GET_PARAMS), dev_id,
                            -1, 0, reinterpret_cast<uint64_t>(data), size,
                            dev_path_len);
}

/**
 * @brief Get the parameters of a ublk device.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param params A pointer to the parameters structure to populate.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto get_params(Fd fd, uint32_t dev_id, ublk_params *params) noexcept {
    return get_params(fd, dev_id, params, sizeof(*params), 0);
}

/**
 * @brief Unprivileged version of start_user_recovery().
 */
template <typename Fd>
inline auto start_user_recovery(Fd fd, uint32_t dev_id, const char *dev_path,
                                uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(
        fd, static_cast<int>(UBLK_U_CMD_START_USER_RECOVERY), dev_id, -1, 0,
        reinterpret_cast<uint64_t>(dev_path), dev_path_len, dev_path_len);
}

/**
 * @brief Start user recovery for a ublk device.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto start_user_recovery(Fd fd, uint32_t dev_id) noexcept {
    return start_user_recovery(fd, dev_id, nullptr, 0);
}

/**
 * @brief Unprivileged version of end_user_recovery().
 */
template <typename Fd>
inline auto end_user_recovery(Fd fd, uint32_t dev_id, int32_t daemon_pid,
                              const char *dev_path,
                              uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_END_USER_RECOVERY),
                            dev_id, -1, static_cast<uint64_t>(daemon_pid),
                            reinterpret_cast<uint64_t>(dev_path), dev_path_len,
                            dev_path_len);
}

/**
 * @brief End user recovery for a ublk device.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param daemon_pid The PID of the daemon process that serves the device.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto end_user_recovery(Fd fd, uint32_t dev_id,
                              int32_t daemon_pid) noexcept {
    return end_user_recovery(fd, dev_id, daemon_pid, nullptr, 0);
}

/**
 * @brief Get the device information of a ublk device, works both for privileged
 * and unprivileged modes.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto get_dev_info2(Fd fd, uint32_t dev_id, void *data, uint16_t size,
                          uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_GET_DEV_INFO2),
                            dev_id, -1, 0, reinterpret_cast<uint64_t>(data),
                            size, dev_path_len);
}

/**
 * @brief Get the feature flags supported by the ublk driver.
 * @param fd The control file descriptor for the ublk device.
 * @param features A pointer to store the returned UBLK_F_* feature flags.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto get_features(Fd fd, uint64_t *features) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_GET_FEATURES), -1,
                            -1, 0, reinterpret_cast<uint64_t>(features),
                            UBLK_FEATURES_LEN, 0);
}

/**
 * @brief Unprivileged version of del_dev_async().
 */
template <typename Fd>
inline auto del_dev_async(Fd fd, uint32_t dev_id, const char *dev_path,
                          uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_DEL_DEV_ASYNC),
                            dev_id, -1, 0, reinterpret_cast<uint64_t>(dev_path),
                            dev_path_len, dev_path_len);
}

/**
 * @brief Delete a ublk device asynchronously.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device to delete.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto del_dev_async(Fd fd, uint32_t dev_id) noexcept {
    return del_dev_async(fd, dev_id, nullptr, 0);
}

/**
 * @brief Unprivileged version of update_size().
 */
template <typename Fd>
inline auto update_size(Fd fd, uint32_t dev_id, uint64_t sectors,
                        const char *dev_path, uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(
        fd, static_cast<int>(UBLK_U_CMD_UPDATE_SIZE), dev_id, -1, sectors,
        reinterpret_cast<uint64_t>(dev_path), dev_path_len, dev_path_len);
}

/**
 * @brief Update the size of a ublk device.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param sectors The new device size in sectors.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto update_size(Fd fd, uint32_t dev_id, uint64_t sectors) noexcept {
    return update_size(fd, dev_id, sectors, nullptr, 0);
}

/**
 * @brief Unprivileged version of quiesce_dev().
 */
template <typename Fd>
inline auto quiesce_dev(Fd fd, uint32_t dev_id, uint64_t timeout_ms,
                        const char *dev_path, uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(
        fd, static_cast<int>(UBLK_U_CMD_QUIESCE_DEV), dev_id, -1, timeout_ms,
        reinterpret_cast<uint64_t>(dev_path), dev_path_len, dev_path_len);
}

/**
 * @brief Quiesce a ublk device (UBLK_F_QUIESCE).
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param timeout_ms The timeout in milliseconds allowed for the device to
 *                   quiesce.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto quiesce_dev(Fd fd, uint32_t dev_id, uint64_t timeout_ms) noexcept {
    return quiesce_dev(fd, dev_id, timeout_ms, nullptr, 0);
}

/**
 * @brief Unprivileged version of try_stop_dev().
 */
template <typename Fd>
inline auto try_stop_dev(Fd fd, uint32_t dev_id, const char *dev_path,
                         uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_TRY_STOP_DEV),
                            dev_id, -1, 0, reinterpret_cast<uint64_t>(dev_path),
                            dev_path_len, dev_path_len);
}

/**
 * @brief Try to stop a ublk device if it has no openers (UBLK_F_SAFE_STOP_DEV).
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device to stop.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto try_stop_dev(Fd fd, uint32_t dev_id) noexcept {
    return try_stop_dev(fd, dev_id, nullptr, 0);
}

/**
 * @brief Unprivileged version of register_shm_buf().
 */
template <typename Fd>
inline auto register_shm_buf(Fd fd, uint32_t dev_id, const void *data,
                             uint16_t size, uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_REG_BUF), dev_id,
                            -1, 0, reinterpret_cast<uint64_t>(data), size,
                            dev_path_len);
}

/**
 * @brief Register a shared memory buffer for zero-copy I/O on a ublk device
 *        (UBLK_F_SHMEM_ZC).
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param reg A pointer to the shared memory buffer registration describing
 *            the buffer to register.
 * @return sender int32_t (the assigned buffer index) on success,
 *         std::error_code on failure.
 */
template <typename Fd>
inline auto register_shm_buf(Fd fd, uint32_t dev_id,
                             const ublk_shmem_buf_reg *reg) noexcept {
    return register_shm_buf(fd, dev_id, reg, sizeof(*reg), 0);
}

/**
 * @brief Unprivileged version of unregister_shm_buf().
 */
template <typename Fd>
inline auto unregister_shm_buf(Fd fd, uint32_t dev_id, uint64_t buf_index,
                               const char *dev_path,
                               uint16_t dev_path_len) noexcept {
    return detail::ctrl_cmd(fd, static_cast<int>(UBLK_U_CMD_UNREG_BUF), dev_id,
                            -1, buf_index, reinterpret_cast<uint64_t>(dev_path),
                            dev_path_len, dev_path_len);
}

/**
 * @brief Unregister a shared memory buffer from a ublk device.
 * @param fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param buf_index The index of the shared memory buffer to unregister, as
 *                  returned by register_shm_buf().
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto unregister_shm_buf(Fd fd, uint32_t dev_id,
                               uint64_t buf_index) noexcept {
    return unregister_shm_buf(fd, dev_id, buf_index, nullptr, 0);
}

/**
 * @brief Overload of fetch_req() with an explicit sqe->addr, used for
 *        automatic request buffer registration (UBLK_F_AUTO_BUF_REG).
 */
template <typename Fd>
inline auto fetch_req(Fd fd, uint16_t q_id, uint16_t tag, uint64_t buf_addr,
                      uint64_t sqe_addr) noexcept {
    return detail::io_cmd(fd, static_cast<int>(UBLK_U_IO_FETCH_REQ), q_id, tag,
                          0, buf_addr, sqe_addr);
}

/**
 * @brief Fetch an I/O request from a ublk queue.
 * @param fd The file descriptor of the ublk queue (/dev/ublkcN).
 * @param q_id The queue ID of the queue to fetch the request from.
 * @param tag The tag identifying the request slot to fetch.
 * @param buf_addr The address of the buffer to receive the request's data.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto fetch_req(Fd fd, uint16_t q_id, uint16_t tag,
                      uint64_t buf_addr) noexcept {
    return fetch_req(fd, q_id, tag, buf_addr, 0);
}

/**
 * @brief Overload of commit_and_fetch_req() with an explicit sqe->addr, used
 *        for automatic request buffer registration (UBLK_F_AUTO_BUF_REG).
 */
template <typename Fd>
inline auto commit_and_fetch_req(Fd fd, uint16_t q_id, uint16_t tag,
                                 int32_t result, uint64_t buf_addr,
                                 uint64_t sqe_addr) noexcept {
    return detail::io_cmd(fd, static_cast<int>(UBLK_U_IO_COMMIT_AND_FETCH_REQ),
                          q_id, tag, result, buf_addr, sqe_addr);
}

/**
 * @brief Commit the result of an I/O request to the driver and fetch the
 *        next request for the same queue.
 * @param fd The file descriptor of the ublk queue (/dev/ublkcN).
 * @param q_id The queue ID of the queue the request belongs to.
 * @param tag The tag of the request whose result is committed.
 * @param result The I/O completion result to commit for the request.
 * @param buf_addr The address of the buffer to receive the next request's
 *                 data.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto commit_and_fetch_req(Fd fd, uint16_t q_id, uint16_t tag,
                                 int32_t result, uint64_t buf_addr) noexcept {
    return commit_and_fetch_req(fd, q_id, tag, result, buf_addr, 0);
}

/**
 * @brief Copy the data of a write request into the user buffer after the
 *        driver returns UBLK_IO_RES_NEED_GET_DATA.
 * @param fd The file descriptor of the ublk queue (/dev/ublkcN).
 * @param q_id The queue ID of the request.
 * @param tag The tag of the request.
 * @param buf_addr The address of the buffer to receive the request data.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto need_get_data(Fd fd, uint16_t q_id, uint16_t tag,
                          uint64_t buf_addr) noexcept {
    return detail::io_cmd(fd, static_cast<int>(UBLK_U_IO_NEED_GET_DATA), q_id,
                          tag, 0, buf_addr, 0);
}

/**
 * @brief Register the buffer of an in-flight request into the io_uring
 *        buffer table for zero-copy I/O (UBLK_F_SUPPORT_ZERO_COPY).
 * @param fd The file descriptor of the ublk queue (/dev/ublkcN).
 * @param q_id The queue ID of the request.
 * @param tag The tag of the request.
 * @param buf_index The index in the io_uring buffer table to install the
 *                  request buffer at.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto register_io_buf(Fd fd, uint16_t q_id, uint16_t tag,
                            uint64_t buf_index) noexcept {
    return detail::io_cmd(fd, static_cast<int>(UBLK_U_IO_REGISTER_IO_BUF), q_id,
                          tag, 0, buf_index, 0);
}

/**
 * @brief Unregister the buffer of an in-flight request from the io_uring
 *        buffer table after zero-copy I/O completes.
 * @param fd The file descriptor of the ublk queue (/dev/ublkcN).
 * @param q_id The queue ID of the request.
 * @param tag The tag of the request.
 * @param buf_index The index in the io_uring buffer table the request buffer
 *                  was installed at.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto unregister_io_buf(Fd fd, uint16_t q_id, uint16_t tag,
                              uint64_t buf_index) noexcept {
    return detail::io_cmd(fd, static_cast<int>(UBLK_U_IO_UNREGISTER_IO_BUF),
                          q_id, tag, 0, buf_index, 0);
}

/**
 * @brief Prepare a batch of I/O commands for a queue (UBLK_F_BATCH_IO).
 * @param fd The file descriptor of the ublk queue (/dev/ublkcN).
 * @param q_id The queue ID of the queue the batch belongs to.
 * @param flags Batch flags (UBLK_BATCH_F_*).
 * @param nr_elem The number of I/O command elements in the batch.
 * @param elem_bytes The size in bytes of each I/O command element.
 * @param elems A pointer to the batch buffer holding the I/O command elements.
 * @param elems_len The size in bytes of the batch buffer.
 * @return sender int32_t on success, std::error_code on failure.
 */
template <typename Fd>
inline auto prep_io_cmds(Fd fd, uint16_t q_id, uint16_t flags, uint16_t nr_elem,
                         uint8_t elem_bytes, const void *elems,
                         uint32_t elems_len) noexcept {
    return detail::batch_io_cmd(fd, static_cast<int>(UBLK_U_IO_PREP_IO_CMDS),
                                q_id, flags, nr_elem, elem_bytes,
                                reinterpret_cast<uint64_t>(elems), elems_len);
}

/**
 * @brief Commit a batch of completed I/O commands for a queue
 *        (UBLK_F_BATCH_IO).
 * @param fd The file descriptor of the ublk queue (/dev/ublkcN).
 * @param q_id The queue ID of the queue the batch belongs to.
 * @param flags Batch flags (UBLK_BATCH_F_*).
 * @param nr_elem The number of I/O command elements in the batch.
 * @param elem_bytes The size in bytes of each I/O command element.
 * @param elems A pointer to the batch buffer holding the I/O command elements.
 * @param elems_len The size in bytes of the batch buffer.
 * @return sender int32_t (the number of bytes of elems actually committed) on
 *         success, std::error_code on failure.
 */
template <typename Fd>
inline auto commit_io_cmds(Fd fd, uint16_t q_id, uint16_t flags,
                           uint16_t nr_elem, uint8_t elem_bytes,
                           const void *elems, uint32_t elems_len) noexcept {
    return detail::batch_io_cmd(fd, static_cast<int>(UBLK_U_IO_COMMIT_IO_CMDS),
                                q_id, flags, nr_elem, elem_bytes,
                                reinterpret_cast<uint64_t>(elems), elems_len);
}

/**
 * @brief Fetch I/O commands in multishot style into a provided buffer queue
 *        (UBLK_F_BATCH_IO).
 * @param fd The file descriptor of the ublk queue (/dev/ublkcN).
 * @param buffers The provided buffer queue that receives the fetched I/O
 *                commands.
 * @param q_id The queue ID of the queue to fetch I/O commands from.
 * @param func The callback invoked for each fetched batch of I/O commands.
 * @return sender (int32_t, condy::BufferInfo) on success, std::error_code on
 * failure.
 */
template <typename Fd, typename MultiShotFunc>
inline auto fetch_io_cmds(Fd fd, condy::ProvidedBufferQueue &buffers,
                          uint16_t q_id, MultiShotFunc &&func) noexcept {
    return condy::async_uring_cmd_multishot(
        static_cast<int>(UBLK_U_IO_FETCH_IO_CMDS), fd,
        [=, bgid = buffers.bgid()](io_uring_sqe *sqe) noexcept {
            auto &b = reinterpret_cast<ublk_batch_io &>(sqe->cmd);
            b = {};
            b.q_id = q_id;
            b.elem_bytes = sizeof(uint16_t);
            sqe->flags |= IOSQE_BUFFER_SELECT;
            sqe->rw_flags |= IORING_URING_CMD_MULTISHOT;
            sqe->buf_group = bgid;
        },
        condy::SelectBufferCQEHandler<condy::ProvidedBufferQueue>{&buffers},
        std::forward<MultiShotFunc>(func));
}

} // namespace raw
} // namespace ublk