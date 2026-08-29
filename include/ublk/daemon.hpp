/**
 * @file daemon.hpp
 * @brief Public high-level API for running a ublk daemon.
 */

#pragma once

#include "ublk/detail/daemon.hpp"
#include "ublk/detail/task.hpp"
#include "ublk/runtime.hpp"

namespace ublk {
/**
 * @brief High-level API for running a ublk daemon.
 */
namespace daemon {

/**
 * @brief Ensure the device described by info has been created and return
 *        its current information.
 * @param control_fd The control file descriptor for the ublk device.
 * @param info Device info of the device to ensure exists. If the device
 * already exists, it is updated with the current information.
 * @return sender bool (true if the device was newly created, false if it
 *         already existed) on success; std::exception_ptr on failure.
 */
inline auto setup(int control_fd, ublksrv_ctrl_dev_info *info) noexcept {
    return detail::task_invoke(detail::daemon_setup_t{}, control_fd, info);
}

/**
 * @brief Ensure the device has been configured and return its current
 *        configuration in params.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param params Configuration to apply when the device is not yet
 *               configured; on success it is written back with the device's
 *               effective parameters.
 * @return sender bool (true if params were applied, false if the device was
 *         already configured) on success; std::exception_ptr on failure.
 */
inline auto configure(int control_fd, uint32_t dev_id,
                      ublk_params *params) noexcept {
    return detail::task_invoke(detail::daemon_configure_t{}, control_fd, dev_id,
                               params);
}

/**
 * @brief Start the device, served by this daemon, if possible.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param daemon_pid The PID of the daemon process serving the device.
 * @return sender () on success, std::exception_ptr on failure.
 */
inline auto start(int control_fd, uint32_t dev_id,
                  int32_t daemon_pid) noexcept {
    return detail::task_invoke(detail::daemon_start_t{}, control_fd, dev_id,
                               daemon_pid);
}

/**
 * @brief Extra options for daemon::run().
 */
struct Options {
    /** @brief Per-queue runtime options, or nullptr for defaults. */
    const RuntimeOptions *runtime_options = nullptr;
    /** @brief Number of registered files in each queue's io_uring. */
    size_t nr_files = 0;
    /** @brief Number of fixed buffers in each queue's io_uring buffer table. */
    size_t nr_buffers = 0;
};

/**
 * @brief Run the daemon serving a device until it stops.
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param handler The IoHandler used to process requests of all queues.
 * @param options Runtime options of the per-queue io_uring loops.
 * @return sender () on success, std::exception_ptr on failure.
 */
template <IoHandler Handler>
inline auto run(int control_fd, uint32_t dev_id, Handler &handler,
                Options options) noexcept {
    return detail::task_invoke(detail::daemon_run_t{}, control_fd, dev_id,
                               &handler, options.runtime_options,
                               options.nr_files, options.nr_buffers);
}

/**
 * @brief Run the shared memory buffer registration service over a Unix
 *        domain socket (UBLK_F_SHMEM_ZC).
 * @param control_fd The control file descriptor for the ublk device.
 * @param dev_id The device ID of the ublk device.
 * @param path The Unix domain socket path to listen on.
 * @param handler The ShmHandler notified of buffer registration and
 *                unregistration.
 * @param flags Flags of the registered buffers (UBLK_SHMEM_BUF_*), e.g.
 *              UBLK_SHMEM_BUF_READ_ONLY.
 * @return sender () on success, std::exception_ptr on failure.
 */
template <ShmHandler Handler>
inline auto run_shm_server(int control_fd, uint32_t dev_id,
                           std::string_view path, Handler &handler,
                           uint32_t flags) noexcept {
    return detail::task_invoke(detail::daemon_shm_server_t{}, control_fd,
                               dev_id, path, &handler, flags);
}

} // namespace daemon
} // namespace ublk