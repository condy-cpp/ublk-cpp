/**
 * @file path.hpp
 * @brief Path related helpers
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace ublk {
namespace detail {

inline void fill_dev_path(char *data, size_t size, uint32_t dev_id) {
    assert(size > 0);
    auto res = std::format_to_n(data, static_cast<std::ptrdiff_t>(size - 1),
                                "/dev/ublkc{}", dev_id);
    *res.out = '\0';
}

inline std::string dev_path(uint32_t dev_id) {
    return std::format("/dev/ublkc{}", dev_id);
}

inline constexpr size_t DEV_PATH_LEN = 32;

template <typename T> struct DevPathBuf {
    static_assert(alignof(T) <= DEV_PATH_LEN,
                  "payload alignment must not exceed DEV_PATH_LEN");
    char path[DEV_PATH_LEN];
    T payload = {};

    DevPathBuf(uint32_t dev_id) { fill_dev_path(path, sizeof(path), dev_id); }
};

} // namespace detail
} // namespace ublk