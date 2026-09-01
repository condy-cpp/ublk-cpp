/**
 * @file shm.hpp
 * @brief Implementation of the shared-memory buffer registration service over
 * a Unix domain socket.
 */

#pragma once

#include "ublk/detail/control.hpp"
#include "ublk/detail/task.hpp"
#include "ublk/handler.hpp"
#include <condy.hpp>
#include <exception>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <utility>

namespace ublk {
namespace detail {

namespace ex = condy::detail::ex;

template <typename Sched, typename Alloc, typename Session>
inline ex::task<void, TaskEnv<Sched, Alloc>>
shm_session(int client_fd, Session session, ex::counting_scope &scope) {
    auto sched = co_await ex::read_env(ex::get_start_scheduler);

    auto d = defer([&]() noexcept { close(client_fd); });

    char data;
    char control[CMSG_SPACE(sizeof(int))];
    iovec iov = {.iov_base = &data, .iov_len = sizeof(data)};
    msghdr msg = {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    co_await condy::async_recvmsg(client_fd, &msg, 0);

    int received_fd = -1;
    for (cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            std::memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(received_fd));
        }
    }

    if (received_fd == -1) {
        throw std::runtime_error("client did not pass a file descriptor");
    }
    auto d2 = defer([&]() noexcept { close(received_fd); });

    auto s = session.handle(received_fd, scope) |
             ex::stopped_as_error(
                 std::error_code(ECANCELED, std::generic_category())) |
             ex::upon_error([](const auto &err) noexcept {
                 return normalize_error(err, EIO);
             }) |
             ex::then([&](auto r) noexcept {
                 using R = decltype(r);
                 if constexpr (std::same_as<R, std::error_code>) {
                     return -r.value();
                 } else {
                     return r;
                 }
             });
    int32_t res = co_await std::move(s);

    auto d3 = defer([&]() noexcept {
        if (res >= 0) {
            session.cleanup(sched, scope);
        }
    });

    co_await condy::async_send(client_fd, condy::buffer(&res, sizeof(res)), 0);

    if (res < 0) {
        throw std::system_error(-res, std::generic_category());
    }

    auto r = co_await condy::async_recv(client_fd,
                                        condy::buffer(&data, sizeof(data)), 0);
    if (r > 0) {
        throw std::runtime_error("client sent data after result");
    }
}

template <typename Sched, typename Alloc, typename Session>
inline ex::task<void, TaskEnv<Sched, Alloc>>
shm_server(int server_fd, Session session, ex::counting_scope &scope) {
    auto sched = co_await ex::read_env(ex::get_start_scheduler);
    while (true) {
        sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = co_await condy::async_accept(
            server_fd, (struct sockaddr *)&client_addr, &client_len, 0);

        auto task = shm_session<Sched, Alloc>(client_fd, session, scope);
        auto s = ex::starts_on(sched, std::move(task)) |
                 ex::upon_error([](const std::exception_ptr &) noexcept {});
        ex::spawn(std::move(s), scope.get_token());
    }
}

template <typename Sched, typename Alloc, typename Session>
inline ex::task<void, TaskEnv<Sched, Alloc>>
shm_server_run(std::string_view path, Session session) {
    int server_fd = co_await condy::async_socket(AF_UNIX, SOCK_STREAM, 0, 0);
    auto d = defer([&] noexcept { close(server_fd); });

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        throw std::length_error("unix path too long");
    }
    std::memcpy(addr.sun_path, path.data(), path.size());
    addr.sun_path[path.size()] = '\0';

    co_await (condy::async_unlink(addr.sun_path, 0) |
              ex::upon_error([](std::error_code ec) {
                  if (ec.value() != ENOENT) {
                      throw std::system_error(ec, "unlink");
                  }
                  return -ec.value();
              }));

    co_await condy::async_bind(server_fd, reinterpret_cast<sockaddr *>(&addr),
                               sizeof(addr));
    auto d2 = defer([&] noexcept { unlink(addr.sun_path); });
    co_await condy::async_listen(server_fd, 128);

    ex::counting_scope scope;
    auto stop_request = [&]() noexcept { scope.request_stop(); };
    ex::inplace_stop_callback<decltype(stop_request)> cb{
        co_await ex::read_env(ex::get_stop_token), std::move(stop_request)};

    std::exception_ptr err;
    auto sched = co_await ex::read_env(ex::get_start_scheduler);
    auto s = ex::starts_on(
                 sched, shm_server<Sched, Alloc>(server_fd, session, scope)) |
             ex::upon_error([&](std::exception_ptr eptr) noexcept {
                 scope.request_stop();
                 err = std::move(eptr);
             });
    ex::spawn(std::move(s), scope.get_token());
    co_await scope.join();
    if (err) {
        std::rethrow_exception(err);
    }
}

template <typename Sched, typename Alloc, ShmHandler Handler> class Session {
public:
    Session(int control_fd, uint32_t dev_id, uint32_t flags, Handler &handler)
        : control_fd_(control_fd), dev_id_(dev_id), flags_(flags),
          handler_(handler) {}

    ex::task<int32_t, TaskEnv<Sched, Alloc>>
    handle(int memfd, ex::counting_scope &scope) noexcept {
        auto sched = co_await ex::read_env(ex::get_start_scheduler);
        bool ok = false;
        auto d = defer([&]() noexcept {
            if (!ok) {
                cleanup(sched, scope);
            }
        });

        struct statx stx;
        co_await condy::async_statx(memfd, "", AT_EMPTY_PATH, STATX_SIZE, &stx);
        size_ = stx.stx_size;

        int map_prot = PROT_READ | PROT_WRITE;
        if (flags_ & UBLK_SHMEM_BUF_READ_ONLY) {
            map_prot = PROT_READ;
        }
        void *addr =
            mmap(nullptr, size_, map_prot, MAP_SHARED | MAP_POPULATE, memfd, 0);
        if (addr == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(),
                                    "memfd mmap");
        }
        base_ = addr;

        ublk_shmem_buf_reg buf_reg = {};
        buf_reg.addr = reinterpret_cast<uint64_t>(base_);
        buf_reg.len = size_;
        buf_reg.flags = flags_;
        index_ = co_await control_register_shm_buf_t{}.invoke<Sched, Alloc>(
            control_fd_, dev_id_, &buf_reg);

        co_await handler_.handle_reg_shm(index_, base_, size_);

        ok = true;
        co_return 0;
    }

    void cleanup(Sched sched, ex::counting_scope &scope) noexcept {
        Session self = *this;
        auto s =
            ex::just() |
            ex::let_value([self = std::move(self)]()
                              -> ex::task<void, TaskEnv<Sched, Alloc>> {
                if (self.index_ != -1) {
                    co_await ex::unstoppable(
                        control_unregister_shm_buf_t{}.invoke<Sched, Alloc>(
                            self.control_fd_, self.dev_id_, self.index_));
                    self.handler_.handle_unreg_shm(self.index_);
                }
                if (self.base_) {
                    munmap(self.base_, self.size_);
                }
            });
        ex::spawn(
            ex::starts_on(sched, std::move(s)) |
                ex::upon_error([](const std::exception_ptr &) noexcept {}),
            scope.get_token());
    }

private:
    int control_fd_;
    uint32_t dev_id_;
    uint32_t flags_;
    Handler &handler_;
    size_t size_ = 0;
    void *base_ = nullptr;
    int32_t index_ = -1;
};

} // namespace detail
} // namespace ublk