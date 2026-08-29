#include "helpers.hpp"
#include "ublk/daemon.hpp"
#include "ublk/ublk_cmd.h"
#include <condy.hpp>
#include <doctest.h>
#include <fcntl.h>
#include <filesystem>
#include <thread>
#include <ublk.hpp>

namespace ex = condy::detail::ex;

TEST_CASE("test daemon") {
    ublk::detail::IoLoop loop(condy::RuntimeOptions().enable_sqe128(), 0, 0,
                              {});
    auto sched = loop.get_scheduler();

    int control_fd = open("/dev/ublk-control", O_RDWR);
    REQUIRE(control_fd >= 0);
    auto d = ublk::detail::defer([&]() noexcept { close(control_fd); });

    uint64_t features = 0;
    ex::sync_wait(
        ex::starts_on(sched, ublk::get_features(control_fd, &features)));
    REQUIRE(features != 0);

    bool unprivileged = !check_privileged();

    uint32_t dev_id = -1;
    ublksrv_ctrl_dev_info info = {};
    uint64_t dev_flags =
        unprivileged
            ? UBLK_F_UNPRIVILEGED_DEV | (UBLK_F_SHMEM_ZC & features)
            : (UBLK_F_USER_RECOVERY | UBLK_F_QUIESCE | UBLK_F_SHMEM_ZC) &
                  features;
    fill_dev_info(info, dev_id, 4, dev_flags);
    ublk_params params = {};
    fill_params(params);

    auto [applyed] =
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        ex::sync_wait(
            ex::starts_on(sched, ublk::daemon::setup(control_fd, &info)))
            .value();
    REQUIRE(applyed);
    dev_id = info.dev_id;
    auto d2 = ublk::detail::defer([&]() noexcept {
        ex::sync_wait(ex::starts_on(sched, ublk::del_dev(control_fd, dev_id)));
    });
    if (unprivileged) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::tie(applyed) =
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        ex::sync_wait(ex::starts_on(sched, ublk::daemon::configure(
                                               control_fd, dev_id, &params)))
            .value();
    REQUIRE(applyed);

    ZeroHandler handler(info.flags);
    ex::simple_counting_scope scope;
    auto task = ublk::daemon::run(control_fd, dev_id, handler, {});
    auto s = ex::starts_on(sched, task) |
             ex::upon_error([&](const std::exception_ptr &ep) noexcept {
                 REQUIRE(false);
             });
    ex::spawn(s, scope.get_token());

    auto d3 =
        ublk::detail::defer([&]() noexcept { ex::sync_wait(scope.join()); });

    ex::sync_wait(ex::starts_on(
        sched, ublk::daemon::start(control_fd, dev_id, getpid())));

    if (unprivileged) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto d4 = ublk::detail::defer([&]() noexcept {
        ex::sync_wait(ex::starts_on(sched, ublk::stop_dev(control_fd, dev_id)));
    });

    auto ublkb_path = std::format("/dev/ublkb{}", dev_id);
    {
        int ublkb_fd = open(ublkb_path.c_str(), O_RDONLY | O_DIRECT);
        REQUIRE(ublkb_fd >= 0);
        auto d = ublk::detail::defer([&]() noexcept { close(ublkb_fd); });
        alignas(4096) char buf[4096];
        ssize_t n = pread(ublkb_fd, buf, sizeof(buf), 0);
        REQUIRE(n == sizeof(buf));
    }

    SUBCASE("recovery") {
        if (!unprivileged && (features & UBLK_F_USER_RECOVERY) &&
            (features & UBLK_F_QUIESCE)) {
            ex::sync_wait(ex::starts_on(
                sched, ublk::quiesce_dev(control_fd, dev_id, 1000)));

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            auto [applyed] =
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                ex::sync_wait(ex::starts_on(sched, ublk::daemon::setup(
                                                       control_fd, &info)))
                    .value();
            REQUIRE(!applyed);
            dev_id = info.dev_id;

            std::tie(applyed) =
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                ex::sync_wait(
                    ex::starts_on(sched, ublk::daemon::configure(
                                             control_fd, dev_id, &params)))
                    .value();
            REQUIRE(!applyed);

            auto task = ublk::daemon::run(control_fd, dev_id, handler, {});
            auto s = ex::starts_on(sched, task) |
                     ex::upon_error([&](const std::exception_ptr &ep) noexcept {
                         REQUIRE(false);
                     });
            ex::spawn(s, scope.get_token());

            ex::sync_wait(ex::starts_on(
                sched, ublk::daemon::start(control_fd, dev_id, getpid())));

            {
                int ublkb_fd = open(ublkb_path.c_str(), O_RDONLY | O_DIRECT);
                REQUIRE(ublkb_fd >= 0);
                auto d =
                    ublk::detail::defer([&]() noexcept { close(ublkb_fd); });
                alignas(4096) char buf[4096];
                ssize_t n = pread(ublkb_fd, buf, sizeof(buf), 0);
                REQUIRE(n == sizeof(buf));
            }
        } else {
            MESSAGE("recovery not supported, skipping");
        }
    }

    SUBCASE("shm_server") {
        if (!(features & UBLK_F_SHMEM_ZC)) {
            MESSAGE("shmem zc not supported, skipping");
        } else {
            auto run_shm_server_test = [&](uint32_t flags) {
                std::string sock_path = make_sock_path();
                std::filesystem::remove(sock_path);

                ex::counting_scope shm_scope;
                auto task = ublk::daemon::run_shm_server(
                    control_fd, dev_id, sock_path, handler, flags);
                ex::spawn(ex::starts_on(sched, task) |
                              ex::upon_error(
                                  [&](const std::exception_ptr &ep) noexcept {
                                      REQUIRE(false);
                                  }),
                          shm_scope.get_token());

                int client_fd = connect_to_server(sock_path);
                auto d_client =
                    ublk::detail::defer([&]() noexcept { close(client_fd); });

                const size_t shm_len = 4096;
                int memfd = create_test_memfd(shm_len);
                auto d_memfd =
                    ublk::detail::defer([&]() noexcept { close(memfd); });
                void *shm = mmap(nullptr, shm_len, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, memfd, 0);
                REQUIRE(shm != MAP_FAILED);
                auto d_shm = ublk::detail::defer(
                    [&]() noexcept { munmap(shm, shm_len); });

                REQUIRE(send_to_server(client_fd, memfd) >= 0);

                int32_t res = recv_result_from_server(client_fd);
                REQUIRE(res == 0);

                {
                    int ublkb_fd =
                        open(ublkb_path.c_str(), O_WRONLY | O_DIRECT);
                    REQUIRE(ublkb_fd >= 0);
                    auto d = ublk::detail::defer(
                        [&]() noexcept { close(ublkb_fd); });
                    ssize_t n = pwrite(ublkb_fd, shm, shm_len, 0);
                    REQUIRE(n == static_cast<ssize_t>(shm_len));
                }
                auto hit = handler.shm_hits();
                REQUIRE(hit >= 1);

                close(client_fd);

                shm_scope.request_stop();
                ex::sync_wait(ex::starts_on(sched, shm_scope.join()));

                {
                    int ublkb_fd =
                        open(ublkb_path.c_str(), O_WRONLY | O_DIRECT);
                    REQUIRE(ublkb_fd >= 0);
                    auto d = ublk::detail::defer(
                        [&]() noexcept { close(ublkb_fd); });
                    ssize_t n = pwrite(ublkb_fd, shm, shm_len, 0);
                    REQUIRE(n == static_cast<ssize_t>(shm_len));
                }
                // No more shm hits after the server is stopped
                REQUIRE(handler.shm_hits() == hit);

                REQUIRE(!std::filesystem::exists(sock_path));
            };

            SUBCASE("shm_server basic") { run_shm_server_test(0); }

            SUBCASE("shm_server buf read only") {
                run_shm_server_test(UBLK_SHMEM_BUF_READ_ONLY);
            }
        }
    }
}