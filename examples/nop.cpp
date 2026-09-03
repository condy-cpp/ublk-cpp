/**
 * @file nop.cpp
 * @brief A no-op ublk block device server
 * @details This example demonstrates the minimal structure of a ublk-cpp
 * daemon.
 */

#include <cerrno>
#include <condy.hpp>
#include <csignal>
#include <cstdint>
#include <exception>
#include <fcntl.h>
#include <getopt.h>
#include <iostream>
#include <print>
#include <sys/signalfd.h>
#include <system_error>
#include <thread>
#include <ublk.hpp>
#include <unistd.h>

namespace ex = condy::detail::ex;

void prep_dev_info(ublksrv_ctrl_dev_info &info, uint32_t dev_id,
                   uint64_t flags) noexcept {
    info = {};
    info.dev_id = dev_id;
    info.nr_hw_queues = 1;
    info.queue_depth = 128;
    info.max_io_buf_bytes = 512 << 10;
    info.flags = flags;
}

void prep_params(ublk_params &params,
                 const ublksrv_ctrl_dev_info &info) noexcept {
    params = {};
    params.len = sizeof(params);
    params.types = UBLK_PARAM_TYPE_BASIC;
    params.basic.logical_bs_shift = 9;
    params.basic.physical_bs_shift = 12;
    params.basic.io_opt_shift = 12;
    params.basic.io_min_shift = 9;
    params.basic.max_sectors = info.max_io_buf_bytes >> 9;
    params.basic.dev_sectors = (250ull * 1024 * 1024 * 1024) >> 9;
}

void print_usage(const char *prog) {
    std::println("Usage: {} -n <dev_id>", prog);
    std::println("Run a no-op ublk block device.");
    std::println();
    std::println("Options:");
    std::println("  -n <dev_id>  ublk device id to use");
    std::println("  -h           show this help and exit");
}

ex::task<void> wait_signal(int ctrl_fd, int signal_fd, uint32_t dev_id,
                           ex::inplace_stop_source &source) {
    std::println("ublk-nop: ublk device {} is running...", dev_id);
    auto parent_token = co_await ex::read_env(ex::get_stop_token);
    ex::inplace_stop_callback cb{parent_token,
                                 [&] noexcept { source.request_stop(); }};
    signalfd_siginfo si;
    co_await (condy::async_read(signal_fd, condy::buffer(&si, sizeof(si)), 0) |
              ex::write_env(ex::prop{ex::get_stop_token, source.get_token()}));
    std::println("ublk-nop: received signal {}, shutting down...",
                 si.ssi_signo);
    co_await ublk::stop_dev(ctrl_fd, dev_id);
}

// NOLINTNEXTLINE(bugprone-unsafe-to-allow-exceptions)
int main(int argc, char *argv[]) noexcept(false) {
    uint32_t dev_id = -1;
    int opt;
    while ((opt = getopt(argc, argv, "n:h")) != -1) {
        switch (opt) {
        case 'n':
            dev_id = std::stoul(optarg);
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

    try {
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);
        sigprocmask(SIG_BLOCK, &mask, nullptr);

        int signal_fd = signalfd(-1, &mask, SFD_NONBLOCK);
        if (signal_fd < 0) {
            throw std::system_error(errno, std::generic_category(), "signalfd");
        }
        auto d = ublk::detail::defer([&] noexcept { close(signal_fd); });

        int ctrl_fd = open("/dev/ublk-control", O_RDWR | O_CLOEXEC);
        if (ctrl_fd < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "open /dev/ublk-control");
        }
        auto d2 = ublk::detail::defer([&] noexcept { close(ctrl_fd); });

        condy::RuntimeOptions options;
        options.enable_sqe128();
        condy::Runtime runtime(options);
        std::jthread loop([&]() { runtime.run(); });
        auto d3 = ublk::detail::defer([&] noexcept { runtime.allow_exit(); });
        ex::scheduler auto sched = condy::get_scheduler(runtime);

        uint64_t features;
        ublksrv_ctrl_dev_info info;
        ublk_params params;
        struct {
            auto handle_io(const ublk::IoData &data) noexcept {
                return ex::just(data.iod->nr_sectors * 512);
            }
        } handler;
        static_assert(ublk::IoHandler<decltype(handler)>);
        ex::inplace_stop_source stop_source;

        ex::sender auto s =
            ublk::get_features(ctrl_fd, &features) |
            ex::let_value([&]() noexcept {
                uint64_t flags = 0;
                if (features & UBLK_F_USER_RECOVERY) {
                    flags |= UBLK_F_USER_RECOVERY;
                }
                prep_dev_info(info, dev_id, flags);
                return ublk::daemon::setup(ctrl_fd, &info);
            }) |
            ex::let_value([&](bool) noexcept {
                prep_params(params, info);
                return ublk::daemon::configure(ctrl_fd, info.dev_id, &params);
            }) |
            ex::let_value([&](bool) noexcept {
                ex::sender auto daemon =
                    ublk::daemon::run(ctrl_fd, info.dev_id, handler, {}) |
                    ex::then([&]() noexcept { stop_source.request_stop(); });
                ex::sender auto start =
                    ublk::daemon::start(ctrl_fd, info.dev_id, getpid()) |
                    ex::let_value([&]() {
                        return wait_signal(ctrl_fd, signal_fd, info.dev_id,
                                           stop_source);
                    });
                return ex::when_all(daemon, start);
            }) |
            ex::let_value(
                [&]() noexcept { return ublk::del_dev(ctrl_fd, info.dev_id); });

        ex::sync_wait(ex::starts_on(sched, s));
    } catch (const std::system_error &e) {
        std::println(std::cerr, "ublk-nop: {}", e.what());
        return e.code().value();
    } catch (const std::exception &e) {
        std::println(std::cerr, "ublk-nop: {}", e.what());
        return 1;
    }

    return 0;
}