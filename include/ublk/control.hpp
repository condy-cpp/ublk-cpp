/**
 * @file control.hpp
 * @brief Public API for ublk control command.
 */

#pragma once

#include "ublk/detail/control.hpp"
#include "ublk/detail/task.hpp"

namespace ublk {

/**
 * @brief Get the queue affinity for a specific ublk queue.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param q_id The queue ID for which to retrieve the affinity information.
 * @param cpuset A pointer to the CPU set to store the affinity information.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto get_queue_affinity(int control_fd, uint32_t dev_id, uint16_t q_id,
                               cpu_set_t *cpuset) noexcept {
    return detail::task_invoke(detail::control_get_queue_affinity_t{},
                               control_fd, dev_id, q_id, cpuset);
}

/**
 * @brief Get the device information for a specific ublk device.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param info A pointer to the device information structure to populate.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto get_dev_info(int control_fd, uint32_t dev_id,
                         ublksrv_ctrl_dev_info *info) noexcept {
    return detail::task_invoke(detail::control_get_dev_info_t{}, control_fd,
                               dev_id, info);
}

/**
 * @brief Add a new ublk device.
 * @param control_fd The control file descriptor for the ublk device.
 * @param info A pointer to the device information structure describing the
 *             device to add.
 * @return sender () on success, std::error_code on failure.
 */
inline auto add_dev(int control_fd, ublksrv_ctrl_dev_info *info) noexcept {
    return raw::add_dev(control_fd, info) |
           detail::ex::then([](int32_t) noexcept {});
}

/**
 * @brief Delete a ublk device.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device to delete.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto del_dev(int control_fd, uint32_t dev_id) noexcept {
    return detail::task_invoke(detail::control_del_dev_t{}, control_fd, dev_id);
}

/**
 * @brief Start a ublk device.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param daemon_pid The PID of the daemon process that serves the device.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto start_dev(int control_fd, uint32_t dev_id,
                      int32_t daemon_pid) noexcept {
    return detail::task_invoke(detail::control_start_dev_t{}, control_fd,
                               dev_id, daemon_pid);
}

/**
 * @brief Stop a ublk device.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device to stop.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto stop_dev(int control_fd, uint32_t dev_id) noexcept {
    return detail::task_invoke(detail::control_stop_dev_t{}, control_fd,
                               dev_id);
}

/**
 * @brief Set the parameters of a ublk device.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param params A pointer to the parameters to apply to the device.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto set_params(int control_fd, uint32_t dev_id,
                       const ublk_params *params) noexcept {
    return detail::task_invoke(detail::control_set_params_t{}, control_fd,
                               dev_id, params);
}

/**
 * @brief Get the parameters of a ublk device.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param params A pointer to the parameters structure to populate.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto get_params(int control_fd, uint32_t dev_id,
                       ublk_params *params) noexcept {
    return detail::task_invoke(detail::control_get_params_t{}, control_fd,
                               dev_id, params);
}

/**
 * @brief Start user recovery for a ublk device.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto start_user_recovery(int control_fd, uint32_t dev_id) noexcept {
    return detail::task_invoke(detail::control_start_user_recovery_t{},
                               control_fd, dev_id);
}

/**
 * @brief End user recovery for a ublk device.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param daemon_pid The PID of the daemon process that serves the device.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto end_user_recovery(int control_fd, uint32_t dev_id,
                              int32_t daemon_pid) noexcept {
    return detail::task_invoke(detail::control_end_user_recovery_t{},
                               control_fd, dev_id, daemon_pid);
}

/**
 * @brief Get the feature flags supported by the ublk driver.
 * @param control_fd The control file descriptor for the ublk device.
 * @param features A pointer to store the returned UBLK_F_* feature flags.
 * @return sender () on success, std::error_code on failure.
 */
inline auto get_features(int control_fd, uint64_t *features) noexcept {
    return raw::get_features(control_fd, features) |
           detail::ex::then([](int32_t) noexcept {});
}

/**
 * @brief Delete a ublk device asynchronously.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device to delete.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto del_dev_async(int control_fd, uint32_t dev_id) noexcept {
    return detail::task_invoke(detail::control_del_dev_async_t{}, control_fd,
                               dev_id);
}

/**
 * @brief Update the size of a ublk device.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param sectors The new device size in sectors.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto update_size(int control_fd, uint32_t dev_id,
                        uint64_t sectors) noexcept {
    return detail::task_invoke(detail::control_update_size_t{}, control_fd,
                               dev_id, sectors);
}

/**
 * @brief Quiesce a ublk device (UBLK_F_QUIESCE).
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param timeout_ms The timeout in milliseconds allowed for the device to
 *                   quiesce.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto quiesce_dev(int control_fd, uint32_t dev_id,
                        uint64_t timeout_ms) noexcept {
    return detail::task_invoke(detail::control_quiesce_dev_t{}, control_fd,
                               dev_id, timeout_ms);
}

/**
 * @brief Try to stop a ublk device if it has no openers (UBLK_F_SAFE_STOP_DEV).
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device to stop.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto try_stop_dev(int control_fd, uint32_t dev_id) noexcept {
    return detail::task_invoke(detail::control_try_stop_dev_t{}, control_fd,
                               dev_id);
}

/**
 * @brief Register a shared memory buffer for zero-copy I/O on a ublk device
 *        (UBLK_F_SHMEM_ZC).
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param reg A pointer to the shared memory buffer registration describing
 *            the buffer to register.
 * @return sender int32_t (the assigned buffer index) on success,
 *         std::exception_ptr on failure.
 */
inline auto register_shm_buf(int control_fd, uint32_t dev_id,
                             const ublk_shmem_buf_reg *reg) noexcept {
    return detail::task_invoke(detail::control_register_shm_buf_t{}, control_fd,
                               dev_id, reg);
}

/**
 * @brief Unregister a shared memory buffer from a ublk device.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param buf_index The index of the shared memory buffer to unregister, as
 *                  returned by register_shm_buf().
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto unregister_shm_buf(int control_fd, uint32_t dev_id,
                               uint64_t buf_index) noexcept {
    return detail::task_invoke(detail::control_unregister_shm_buf_t{},
                               control_fd, dev_id, buf_index);
}

} // namespace ublk