#include "helpers.hpp"
#include <condy.hpp>
#include <cstring>
#include <doctest.h>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <ublk/detail/shm.hpp>
#include <unistd.h>

namespace ex = condy::detail::ex;

namespace {

struct MockSession {
    bool &handle_called;
    int &received_fd;
    int32_t result;
    bool &should_throw;

    ex::task<int32_t> handle(int memfd, ex::counting_scope &) noexcept {
        handle_called = true;
        received_fd = memfd;
        // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
        if (should_throw) {
            throw std::system_error(EIO, std::generic_category());
        }
        co_return result;
    }

    void cleanup(condy::Scheduler, ex::counting_scope &) noexcept {}
};

template <typename Session>
inline void spawn_shm_server(condy::Scheduler sched, ex::counting_scope &scope,
                             std::string_view path, Session session) {
    ex::spawn(
        ex::starts_on(sched,
                      ublk::detail::shm_server_run<condy::Scheduler,
                                                   std::allocator<std::byte>>(
                          path, std::move(session))) |
            ex::upon_error([](const auto &) noexcept {}),
        scope.get_token());
}

} // namespace

TEST_CASE("test shm") {
    ublk::detail::IoLoop loop(condy::RuntimeOptions().enable_sqe128(), 0, 0,
                              {});
    auto sched = loop.get_scheduler();

    std::string sock_path = make_sock_path();
    std::filesystem::remove(sock_path);

    bool handle_called = false;
    int received_fd = -1;
    int32_t expected_result = 42;
    bool should_throw = false;
    MockSession session{handle_called, received_fd, expected_result,
                        should_throw};

    SUBCASE("path too long") {
        std::string long_path(500, 'x');
        REQUIRE_THROWS_AS(
            ex::sync_wait(ex::starts_on(
                sched, ublk::detail::shm_server_run<condy::Scheduler,
                                                    std::allocator<std::byte>>(
                           long_path, session))),
            std::length_error);
    }

    SUBCASE("startup and shutdown") {
        ex::counting_scope scope;
        spawn_shm_server(sched, scope, sock_path, session);

        SUBCASE("client connects and sends fd") {
            int client_fd = connect_to_server(sock_path);
            auto d = ublk::detail::defer([&]() noexcept { close(client_fd); });

            int memfd = create_test_memfd(4096);
            auto d2 = ublk::detail::defer([&]() noexcept { close(memfd); });
            REQUIRE(send_to_server(client_fd, memfd) >= 0);

            int32_t result = recv_result_from_server(client_fd);
            REQUIRE(result == expected_result);
        }

        SUBCASE("session handle throws error") {
            // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
            should_throw = true;
            int client_fd = connect_to_server(sock_path);
            auto d = ublk::detail::defer([&]() noexcept { close(client_fd); });

            int memfd = create_test_memfd(4096);
            auto d2 = ublk::detail::defer([&]() noexcept { close(memfd); });
            REQUIRE(send_to_server(client_fd, memfd) >= 0);

            int32_t result = recv_result_from_server(client_fd);
            REQUIRE(result < 0);
        }

        SUBCASE("client close without sending fd") {
            int client_fd = connect_to_server(sock_path);
            close(client_fd);
        }

        SUBCASE("client sends data without fd") {
            int client_fd = connect_to_server(sock_path);
            auto d = ublk::detail::defer([&]() noexcept { close(client_fd); });

            // Send plain data without fd
            REQUIRE(send_to_server(client_fd) >= 0);
            // Server closes connection
            verify_connection_closed(client_fd);
        }

        SUBCASE("client sends data after result") {
            int client_fd = connect_to_server(sock_path);
            auto d = ublk::detail::defer([&]() noexcept { close(client_fd); });

            int memfd = create_test_memfd(4096);
            auto d2 = ublk::detail::defer([&]() noexcept { close(memfd); });
            REQUIRE(send_to_server(client_fd, memfd) >= 0);

            int32_t result = recv_result_from_server(client_fd);
            REQUIRE(result == expected_result);

            // Send data after result
            REQUIRE(send_to_server(client_fd) >= 0);
            // Server closes connection
            verify_connection_closed(client_fd);
        }

        scope.request_stop();
        ex::sync_wait(ex::starts_on(sched, scope.join()));

        REQUIRE(!std::filesystem::exists(sock_path));
    }
}