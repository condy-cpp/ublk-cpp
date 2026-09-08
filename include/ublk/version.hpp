/**
 * @file version.hpp
 * @brief Library version macros and constants.
 */

#pragma once

/** @brief ublk-cpp major version. */
#define UBLKCPP_VERSION_MAJOR 0

/** @brief ublk-cpp minor version. */
#define UBLKCPP_VERSION_MINOR 1

namespace ublk {

/** @brief ublk-cpp major version. */
inline constexpr int version_major = UBLKCPP_VERSION_MAJOR;

/** @brief ublk-cpp minor version. */
inline constexpr int version_minor = UBLKCPP_VERSION_MINOR;

} // namespace ublk
