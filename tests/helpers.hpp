#pragma once

#include "ublk/helpers.hpp"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <condy.hpp>
#include <cstdint>
#include <cstring>
#include <doctest.h>
#include <fcntl.h>
#include <format>
#include <linux/capability.h>
#include <string>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <thread>
#include <ublk.hpp>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace {

inline std::string make_sock_path() {
    static std::atomic<uint64_t> counter{0};
    return std::format("/tmp/test-shm-{}-{}.sock", getpid(),
                       counter.fetch_add(1));
}

inline int create_test_memfd(size_t size) {
    int fd = memfd_create("test-shm", 0);
    REQUIRE(fd >= 0);
    REQUIRE(ftruncate(fd, static_cast<off_t>(size)) == 0);
    return fd;
}

inline int connect_to_server(const std::string &sock_path,
                             size_t max_retry = 100) {
    for (size_t i = 0; i < max_retry; ++i) {
        int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        REQUIRE(client_fd >= 0);

        sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, sock_path.data(), sock_path.size());

        if (connect(client_fd, reinterpret_cast<sockaddr *>(&addr),
                    sizeof(addr)) == 0) {
            return client_fd;
        }
        close(client_fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FAIL("failed to connect to server");
    return -1;
}

inline ssize_t send_to_server(int sock_fd, int fd_to_send = -1) {
    char data = 'x';
    if (fd_to_send >= 0) {
        char control[CMSG_SPACE(sizeof(int))];
        iovec iov = {.iov_base = &data, .iov_len = sizeof(data)};
        msghdr msg = {};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        std::memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(fd_to_send));

        return sendmsg(sock_fd, &msg, 0);
    } else {
        return send(sock_fd, &data, sizeof(data), MSG_NOSIGNAL);
    }
}

inline int32_t recv_result_from_server(int sock_fd) {
    int32_t result;
    ssize_t r = recv(sock_fd, &result, sizeof(result), 0);
    REQUIRE(r == sizeof(result));
    return result;
}

inline void verify_connection_closed(int sock_fd) {
    int32_t dummy;
    ssize_t r = recv(sock_fd, &dummy, sizeof(dummy), 0);
    REQUIRE(r == 0);
}

inline void fill_dev_info(ublksrv_ctrl_dev_info &info, uint32_t dev_id,
                          uint16_t nr_hw_queues, uint64_t flags) noexcept {
    info = {};
    info.dev_id = dev_id;
    info.nr_hw_queues = nr_hw_queues;
    info.queue_depth = 32;
    info.max_io_buf_bytes = 64u << 10;
    info.flags = flags;
}

inline void fill_params(ublk_params &params) noexcept {
    params = {};
    params.len = sizeof(params);
    params.types = UBLK_PARAM_TYPE_BASIC;
    params.basic.attrs = 0;
    params.basic.logical_bs_shift = 9;
    params.basic.physical_bs_shift = 9;
    params.basic.io_opt_shift = 9;
    params.basic.io_min_shift = 9;
    params.basic.max_sectors = (64u << 10) / 512;
    params.basic.dev_sectors = (64ull << 20) / 512;
}

inline bool check_privileged() noexcept {
    struct __user_cap_header_struct hdr = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct __user_cap_data_struct data[2] = {};
    if (syscall(SYS_capget, &hdr, data) < 0) {
        return false;
    }
    return data[0].effective & (1U << CAP_SYS_ADMIN);
}

struct ZeroHandler {
    ZeroHandler(uint64_t flags) noexcept : flags_(flags) {
        zero_fd = open("/dev/zero", O_RDONLY);
        REQUIRE(zero_fd >= 0);
    }
    ~ZeroHandler() { close(zero_fd); }

    ZeroHandler(const ZeroHandler &) = delete;
    ZeroHandler &operator=(const ZeroHandler &) = delete;

    condy::detail::ex::task<void> handle_reg_shm(int32_t index, void *base,
                                                 size_t size) noexcept {
        shm_bufs_[index] = ShmBuf{base, size};
        co_return;
    }

    void handle_unreg_shm(int32_t index) noexcept { shm_bufs_.erase(index); }

    condy::detail::ex::task<int32_t>
    handle_io(const ublk::IoData &data) noexcept {
        uint64_t start = data.iod->start_sector * 512;
        uint32_t nbytes = data.iod->nr_sectors * 512;

        int32_t r = nbytes;
        auto op = ublksrv_get_op(data.iod);
        if (op == UBLK_IO_OP_WRITE) {
            if (data.iod->op_flags & UBLK_IO_F_SHMEM_ZC) {
                shm_hits_.fetch_add(1, std::memory_order_relaxed);
            }
            co_return r;
        }
        assert(op == UBLK_IO_OP_READ);
        if (data.iod->op_flags & UBLK_IO_F_SHMEM_ZC) {
            shm_hits_.fetch_add(1, std::memory_order_relaxed);
            uint16_t idx = ublk::shmem_zc_index(data.iod->addr);
            uint32_t off = ublk::shmem_zc_offset(data.iod->addr);
            auto it = shm_bufs_.find(idx);
            assert(it != shm_bufs_.end());
            co_await condy::async_read(
                zero_fd,
                condy::buffer(static_cast<char *>(it->second.base) + off,
                              nbytes),
                start);
        } else if (data.buf != nullptr) {
            co_await condy::async_read(zero_fd, condy::buffer(data.buf, nbytes),
                                       start);
        } else if (flags_ & UBLK_F_SUPPORT_ZERO_COPY) {
            co_await condy::async_read(
                zero_fd,
                condy::fixed(
                    data.tag,
                    condy::buffer(static_cast<void *>(nullptr), nbytes)),
                start);
        } else if (flags_ & UBLK_F_USER_COPY) {
            std::vector<char> tmp(nbytes);
            co_await condy::async_read(
                zero_fd, condy::buffer(tmp.data(), nbytes), start);
            uint64_t pos = ublk::user_copy_pos(data.q_id, data.tag, 0);
            co_await condy::async_write(condy::fixed(0),
                                        condy::buffer(tmp.data(), nbytes), pos);
        } else {
            r = -EINVAL;
        }
        co_return r;
    }

    size_t shm_hits() const noexcept {
        return shm_hits_.load(std::memory_order_relaxed);
    }

private:
    struct ShmBuf {
        void *base = nullptr;
        size_t size = 0;
    };
    std::unordered_map<int32_t, ShmBuf> shm_bufs_;
    int zero_fd = -1;
    uint64_t flags_ = 0;
    std::atomic<size_t> shm_hits_{0};
};

} // namespace
