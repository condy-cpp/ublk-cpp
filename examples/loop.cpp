/**
 * @file loop.cpp
 * @brief A ublk loop device server
 * @details Maps a regular file or block device into a block device,
 * similar to the kernel loop driver.
 */

#include <bit>
#include <cerrno>
#include <condy.hpp>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <getopt.h>
#include <iostream>
#include <linux/fs.h>
#include <print>
#include <string>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <ublk.hpp>
#include <unistd.h>

namespace ex = condy::detail::ex;

namespace {

constexpr size_t SECTOR_SIZE = 512;
constexpr uint16_t DEFAULT_QUEUE_DEPTH = 64;
constexpr uint32_t DEFAULT_IO_BUF_BYTES = 512u << 10;

struct FileInfo {
    uint64_t size = 0;
    uint8_t logical_bs_shift = 9;
    uint8_t physical_bs_shift = 12;
};

uint8_t bs_shift_of(uint64_t v) noexcept {
    if (v > 0 && (v & (v - 1)) == 0) {
        return static_cast<uint8_t>(std::bit_width(v) - 1);
    }
    return 9;
}

void lo_file_size(int fd, FileInfo &info) {
    struct stat st;
    if (fstat(fd, &st) < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "fstat backing file");
    }

    if (S_ISBLK(st.st_mode)) {
        uint64_t size = 0;
        int lsz = 0;
        int psz = 0;
        if (ioctl(fd, BLKGETSIZE64, &size) < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "BLKGETSIZE64");
        }
        if (ioctl(fd, BLKSSZGET, &lsz) < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "BLKSSZGET");
        }
        if (ioctl(fd, BLKPBSZGET, &psz) < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "BLKPBSZGET");
        }
        info.size = size;
        info.logical_bs_shift = bs_shift_of(static_cast<uint64_t>(lsz));
        info.physical_bs_shift = bs_shift_of(static_cast<uint64_t>(psz));
        return;
    }

    if (S_ISREG(st.st_mode)) {
        info.size = static_cast<uint64_t>(st.st_size);
        info.logical_bs_shift = 9;
        info.physical_bs_shift = 12;
        return;
    }

    throw std::runtime_error("unsupported backing file type");
}

void prep_dev_info(ublksrv_ctrl_dev_info &info, uint32_t dev_id,
                   uint16_t nr_queues, uint16_t depth, uint32_t buf_bytes,
                   uint64_t flags) noexcept {
    info = {};
    info.dev_id = dev_id;
    info.nr_hw_queues = nr_queues;
    info.queue_depth = depth;
    info.max_io_buf_bytes = buf_bytes;
    info.flags = flags;
}

void prep_params(ublk_params &params, const ublksrv_ctrl_dev_info &info,
                 uint64_t dev_sectors, uint8_t logical_bs_shift,
                 uint8_t physical_bs_shift) noexcept {
    params = {};
    params.len = sizeof(params);
    params.types = UBLK_PARAM_TYPE_BASIC;
    params.basic.attrs = UBLK_ATTR_VOLATILE_CACHE;
    params.basic.logical_bs_shift = logical_bs_shift;
    params.basic.physical_bs_shift = physical_bs_shift;
    params.basic.io_opt_shift = 12;
    params.basic.io_min_shift = 9;
    params.basic.max_sectors = info.max_io_buf_bytes >> 9;
    params.basic.dev_sectors = dev_sectors;
}

struct LoopHandler {
    static constexpr int BACKING_FD = 1;

    int backing_fd = -1;

    auto init_queue(uint16_t /*q_id*/) noexcept {
        int fd = backing_fd;
        return ex::just() | ex::then([fd]() {
                   auto &fd_table = condy::current_runtime().fd_table();
                   int r = fd_table.update(BACKING_FD, &fd, 1);
                   if (r != 1) {
                       throw std::system_error(
                           -r, std::generic_category(),
                           "ublk-loop: register backing file");
                   }
               });
    }

    void destroy_queue(uint16_t /*q_id*/) noexcept {
        int fd = -1;
        auto &fd_table = condy::current_runtime().fd_table();
        fd_table.update(BACKING_FD, &fd, 1);
    }

    ex::task<int32_t> handle_io(const ublk::IoData &data) noexcept {
        uint8_t op = ublksrv_get_op(data.iod);
        uint64_t off =
            static_cast<uint64_t>(data.iod->start_sector) * SECTOR_SIZE;
        uint32_t bytes = data.iod->nr_sectors * SECTOR_SIZE;

        switch (op) {
        case UBLK_IO_OP_READ:
            co_return co_await condy::async_read(
                condy::fixed(BACKING_FD), condy::buffer(data.buf, bytes), off);
        case UBLK_IO_OP_WRITE:
            co_return co_await condy::async_write(
                condy::fixed(BACKING_FD), condy::buffer(data.buf, bytes), off);
        case UBLK_IO_OP_FLUSH:
            co_return co_await condy::async_fsync(condy::fixed(BACKING_FD),
                                                  IORING_FSYNC_DATASYNC);
        default:
            co_return -EINVAL;
        }
    }
};

static_assert(ublk::IoHandler<LoopHandler>);
static_assert(ublk::QueueHandler<LoopHandler>);

ex::task<void> wait_signal(int ctrl_fd, int signal_fd, uint32_t dev_id,
                           ex::inplace_stop_source &source) {
    std::println("ublk-loop: ublk device {} is running...", dev_id);
    auto parent_token = co_await ex::read_env(ex::get_stop_token);
    auto stop_request = [&] noexcept { source.request_stop(); };
    ex::inplace_stop_callback<decltype(stop_request)> cb{
        parent_token, std::move(stop_request)};
    signalfd_siginfo si;
    co_await (condy::async_read(signal_fd, condy::buffer(&si, sizeof(si)), 0) |
              ex::write_env(ex::prop{ex::get_stop_token, source.get_token()}));
    std::println("ublk-loop: received signal {}, shutting down...",
                 si.ssi_signo);
    co_await ublk::stop_dev(ctrl_fd, dev_id);
}

void print_usage(const char *prog) {
    std::println("Usage: {} [OPTIONS] -f <backing_file>", prog);
    std::println(
        "Run a ublk loop device backed by a regular file or block device.");
    std::println();
    std::println("Options:");
    std::println("  -f <file>     backing file or block device (required)");
    std::println(
        "  -n <dev_id>   ublk device id, -1: auto-allocation (default)");
    std::println("  -q <queues>   nr_hw_queues (default: 1)");
    std::println(
        "  -d <depth>    queue depth, max in-flight io commands (default: 64)");
    std::println("  -b <bytes>    io buffer size (default: 524288)");
    std::println(
        "  -p            enable UBLK_F_UNPRIVILEGED_DEV (unprivileged mode)");
    std::println("  -h            show this help and exit");
}

} // namespace

// NOLINTNEXTLINE(bugprone-unsafe-to-allow-exceptions)
int main(int argc, char *argv[]) noexcept(false) {
    uint32_t dev_id = -1;
    uint16_t nr_queues = 1;
    uint16_t queue_depth = DEFAULT_QUEUE_DEPTH;
    uint32_t buf_bytes = DEFAULT_IO_BUF_BYTES;
    bool unprivileged = false;
    std::string backing_path;

    int opt;
    while ((opt = getopt(argc, argv, "f:n:q:d:b:ph")) != -1) {
        switch (opt) {
        case 'f':
            backing_path = optarg;
            break;
        case 'n':
            dev_id = std::stoul(optarg);
            break;
        case 'q':
            nr_queues = std::stoul(optarg);
            break;
        case 'd':
            queue_depth = std::stoul(optarg);
            break;
        case 'b':
            buf_bytes = std::stoul(optarg);
            break;
        case 'p':
            unprivileged = true;
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

    if (backing_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        int backing_fd = open(backing_path.c_str(), O_RDWR);
        if (backing_fd < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "open backing file");
        }
        auto d_backing =
            ublk::detail::defer([&] noexcept { close(backing_fd); });

        int flags = fcntl(backing_fd, F_GETFL);
        if (flags >= 0 && fcntl(backing_fd, F_SETFL, flags | O_DIRECT) < 0) {
            std::println(
                std::cerr,
                "ublk-loop: failed to set O_DIRECT on backing file: {}",
                std::strerror(errno));
        }

        FileInfo file_info;
        lo_file_size(backing_fd, file_info);
        uint64_t dev_sectors = file_info.size / SECTOR_SIZE;
        std::println("ublk-loop: backing file {} ({} bytes, {} sectors, "
                     "logical {}B, physical {}B)",
                     backing_path, file_info.size, dev_sectors,
                     1u << file_info.logical_bs_shift,
                     1u << file_info.physical_bs_shift);

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
        LoopHandler handler{backing_fd};
        ex::inplace_stop_source stop_source;

        ex::sender auto s =
            ublk::get_features(ctrl_fd, &features) |
            ex::let_value([&]() noexcept {
                uint64_t flags = 0;
                if (features & UBLK_F_USER_RECOVERY) {
                    flags |= UBLK_F_USER_RECOVERY;
                }
                if (unprivileged) {
                    flags |= UBLK_F_UNPRIVILEGED_DEV;
                }
                prep_dev_info(info, dev_id, nr_queues, queue_depth, buf_bytes,
                              flags);
                return ublk::daemon::setup(ctrl_fd, &info);
            }) |
            ex::let_value([&](bool) noexcept {
                prep_params(params, info, dev_sectors,
                            file_info.logical_bs_shift,
                            file_info.physical_bs_shift);
                return ublk::daemon::configure(ctrl_fd, info.dev_id, &params);
            }) |
            ex::let_value([&](bool) noexcept {
                ex::sender auto daemon =
                    ublk::daemon::run(ctrl_fd, info.dev_id, handler,
                                      {.nr_files = 2}) |
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
        std::println(std::cerr, "ublk-loop: {}", e.what());
        return e.code().value();
    } catch (const std::exception &e) {
        std::println(std::cerr, "ublk-loop: {}", e.what());
        return 1;
    }

    return 0;
}
