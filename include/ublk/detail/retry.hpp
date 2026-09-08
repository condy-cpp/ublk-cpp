#pragma once

#include "ublk/detail/task.hpp"
#include <condy.hpp>
#include <cstddef>
#include <cstdint>
#include <system_error>

namespace ublk {
namespace detail {

namespace ex = condy::detail::ex;

template <typename Sched, typename Alloc, typename Fn>
inline ex::task<bool, TaskEnv<Sched, Alloc>> retry(Fn fn, size_t max_retry,
                                                   int64_t sleep_ms) {
    if (co_await fn()) {
        co_return true;
    }
    __kernel_timespec ts = {};
    ts.tv_sec = sleep_ms / 1000;
    ts.tv_nsec = (sleep_ms % 1000) * 1'000'000;
    bool ok = false;
    for (size_t i = 0; i < max_retry; i++) {
        co_await (condy::async_timeout(&ts, 0, 0) |
                  ex::upon_error(
                      [](std::error_code ec) noexcept { return -ec.value(); }));
        ok = co_await fn();
        if (ok) {
            break;
        }
    }
    co_return ok;
}

} // namespace detail
} // namespace ublk