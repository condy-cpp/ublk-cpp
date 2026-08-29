/**
 * @file runtime.hpp
 * @brief Runtime options based on condy::RuntimeOptions.
 */

#pragma once

#include <condy.hpp>

namespace ublk {

/**
 * @brief Runtime options for ublk device.
 */
class RuntimeOptions : public condy::RuntimeOptions {
public:
    /**
     * @brief Set SQ size
     * @param v SQ size
     */
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    RuntimeOptions &sq_size(size_t v) noexcept {
        requested_sq_size_ = v;
        condy::RuntimeOptions::sq_size(v);
        return *this;
    }

    /**
     * @brief Set CQ size
     * @param v CQ size
     */
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    RuntimeOptions &cq_size(size_t v) noexcept {
        requested_cq_size_ = v;
        condy::RuntimeOptions::cq_size(v);
        return *this;
    }

    condy::RuntimeOptions build(size_t default_sq_size) const noexcept {
        condy::RuntimeOptions o = *this;
        o.sq_size(std::max(requested_sq_size_, default_sq_size));
        o.cq_size(std::max(requested_cq_size_, default_sq_size * 2));
        return o;
    }

private:
    size_t requested_sq_size_ = 0;
    size_t requested_cq_size_ = 0;
};

} // namespace ublk