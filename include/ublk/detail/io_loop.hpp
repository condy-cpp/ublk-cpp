/**
 * @file io_loop.hpp
 * @brief Helpful condy runtime wrapper for running a dedicated I/O loop on a
 * pinned thread.
 */

#pragma once

#include <condy.hpp>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <thread>

namespace ublk {
namespace detail {

class IoLoop {
public:
    IoLoop(const condy::RuntimeOptions &options, size_t nr_files,
           size_t nr_buffers, cpu_set_t cpuset)
        : runtime_(options) {
        if (nr_files) {
            int r = runtime_.fd_table().init(nr_files);
            if (r < 0) {
                throw std::system_error(-r, std::generic_category(),
                                        "fd_table init");
            }
        }
        if (nr_buffers) {
            int r = runtime_.buffer_table().init(nr_buffers);
            if (r < 0) {
                throw std::system_error(-r, std::generic_category(),
                                        "buffer_table init");
            }
        }
        thread_ = std::jthread([this, cpuset] { run_(cpuset); });
    }

    IoLoop(const IoLoop &) = delete;
    IoLoop &operator=(const IoLoop &) = delete;
    IoLoop(IoLoop &&) = delete;
    IoLoop &operator=(IoLoop &&) = delete;

    ~IoLoop() { runtime_.allow_exit(); }

public:
    auto get_scheduler() noexcept { return condy::get_scheduler(runtime_); }

    auto &runtime() noexcept { return runtime_; }

private:
    void run_(cpu_set_t cpuset) {
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        setpriority(PRIO_PROCESS, 0, -20);
#if defined(PR_SET_IO_FLUSHER)
        prctl(PR_SET_IO_FLUSHER, 1, 0, 0, 0);
#endif
        runtime_.run();
    }

private:
    condy::Runtime runtime_;
    std::jthread thread_;
};

} // namespace detail
} // namespace ublk