/**
 * @file execution.hpp
 * @brief The std::execution backend namespace used by ublk-cpp.
 */

#pragma once

#include <condy.hpp>

#ifndef CONDY_DETAIL_HAS_EXECUTION
#error "ublk-cpp requires a condy backend for std::execution"
#endif

namespace ublk {
namespace detail {

namespace ex = condy::detail::ex;

} // namespace detail
} // namespace ublk
