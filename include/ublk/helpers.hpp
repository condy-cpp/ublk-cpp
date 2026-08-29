/**
 * @file helpers.hpp
 * @brief Simple ublk helper functions.
 */

#pragma once

#include "ublk/ublk_cmd.h"
#include <cassert>
#include <cstdint>

namespace ublk {

/**
 * @brief Extract the buffer index from a shared memory zero-copy encoded
 *        address (UBLK_IO_F_SHMEM_ZC).
 * @param addr The encoded addr field of an I/O descriptor, set by the driver
 *             when a request's pages match a registered shared memory buffer.
 * @return The buffer index.
 */
inline uint16_t shmem_zc_index(uint64_t addr) noexcept {
    return ublk_shmem_zc_index(addr);
}

/**
 * @brief Extract the byte offset within the buffer from a shared memory
 *        zero-copy encoded address (UBLK_IO_F_SHMEM_ZC).
 * @param addr The encoded addr field of an I/O descriptor, set by the driver
 *             when a request's pages match a registered shared memory buffer.
 * @return The byte offset.
 */
inline uint32_t shmem_zc_offset(uint64_t addr) noexcept {
    return ublk_shmem_zc_offset(addr);
}

/**
 * @brief Compute the byte position in /dev/ublkcN for user-space data copy
 *        (UBLK_F_USER_COPY).
 * @param q_id The queue ID of the request.
 * @param tag The tag of the request.
 * @param offset The byte offset within the request buffer.
 * @return The absolute byte position to pass to pread()/pwrite() for
 *         accessing the request data at offset.
 */
inline uint64_t user_copy_pos(uint16_t q_id, uint16_t tag,
                              uint32_t offset) noexcept {
    assert(!(offset & ~UBLK_IO_BUF_BITS_MASK));
    return UBLKSRV_IO_BUF_OFFSET +
           ((static_cast<uint64_t>(q_id) << UBLK_QID_OFF) |
            (static_cast<uint64_t>(tag) << UBLK_TAG_OFF) | offset);
}

} // namespace ublk