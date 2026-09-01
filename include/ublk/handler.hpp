/**
 * @file handler.hpp
 * @brief Handler concepts for ublk server.
 */

#pragma once

#include "ublk/ublk_cmd.h"
#include <concepts>
#include <condy.hpp>
#include <cstddef>
#include <cstdint>

namespace ublk {

namespace detail {
namespace ex = condy::detail::ex;
}

/**
 * @brief Context of a single I/O request delivered to a handler.
 */
struct IoData {
    /** @brief Queue ID of the request. */
    uint16_t q_id;
    /** @brief Tag of the request. */
    uint16_t tag;
    /** @brief I/O descriptor of the request in shared memory. */
    const ublksrv_io_desc *iod;
    /** @brief Buffer for the request data. */
    void *buf;
};

/** @brief Concept of an async handler with handle_io(const IoData&). */
template <typename T>
concept IoHandler = requires(T &t, const IoData &io_data) {
    { t.handle_io(io_data) } noexcept -> detail::ex::sender;
};

/** @brief Concept of a handler with per-queue init/destroy callbacks. */
template <typename T>
concept QueueHandler = requires(T &t, uint16_t q_id) {
    { t.init_queue(q_id) } noexcept -> detail::ex::sender;
    { t.destroy_queue(q_id) } noexcept -> std::same_as<void>;
};

/** @brief Concept of a handler notified of shared memory buffer registration.
 */
template <typename T>
concept ShmHandler = requires(T &t, int32_t index, void *base, size_t size) {
    { t.handle_reg_shm(index, base, size) } noexcept -> detail::ex::sender;
    { t.handle_unreg_shm(index) } noexcept -> std::same_as<void>;
};

} // namespace ublk