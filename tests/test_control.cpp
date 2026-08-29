#include "helpers.hpp"
#include <chrono>
#include <condy.hpp>
#include <doctest.h>
#include <fcntl.h>
#include <system_error>
#include <thread>
#include <ublk.hpp>
#include <unistd.h>

namespace ex = condy::detail::ex;

TEST_CASE("test control") {
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

    SUBCASE("add_dev / del_dev with dev_id") {
        uint32_t dev_id = 1024;
        ublksrv_ctrl_dev_info info = {};
        fill_dev_info(info, dev_id, 1,
                      unprivileged ? UBLK_F_UNPRIVILEGED_DEV : 0);
        ex::sync_wait(ex::starts_on(sched, ublk::add_dev(control_fd, &info)));
        REQUIRE(info.dev_id == dev_id);
        if (unprivileged) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ex::sync_wait(
            ex::starts_on(sched, ublk::del_dev(control_fd, info.dev_id)));
    }

    SUBCASE("add_dev / del_dev") {
        uint32_t dev_id = -1;
        ublksrv_ctrl_dev_info info = {};
        uint64_t dev_flags =
            unprivileged ? UBLK_F_UNPRIVILEGED_DEV |
                               (UBLK_F_UPDATE_SIZE | UBLK_F_SHMEM_ZC) & features
                         : (UBLK_F_USER_RECOVERY | UBLK_F_UPDATE_SIZE |
                            UBLK_F_QUIESCE | UBLK_F_SHMEM_ZC) &
                               features;
        fill_dev_info(info, dev_id, 1, dev_flags);

        ex::sync_wait(ex::starts_on(sched, ublk::add_dev(control_fd, &info)));
        REQUIRE(info.dev_id != dev_id);
        dev_id = info.dev_id;

        auto d = ublk::detail::defer([&]() noexcept {
            ex::sync_wait(ex::starts_on(
                sched, ublk::del_dev(control_fd, dev_id) |
                           ex::upon_error(
                               [](const std::exception_ptr &) noexcept {})));
        });

        if (unprivileged) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        SUBCASE("del_dev_async") {
            // TODO: Kernel bug
            // https://lore.kernel.org/linux-block/20260901142438.237586-1-yi1tang.yang@gmail.com/
            if (!unprivileged) {
                // Hold dev reference
                auto ublkc_path = ublk::detail::dev_path(dev_id);
                int ublkc_fd = open(ublkc_path.c_str(), O_RDWR);
                auto d =
                    ublk::detail::defer([&]() noexcept { close(ublkc_fd); });

                try {
                    ex::sync_wait(ex::starts_on(
                        sched, ublk::del_dev_async(control_fd, dev_id)));
                } catch (const std::system_error &e) {
                    if (e.code().value() != EOPNOTSUPP) {
                        throw;
                    }
                    MESSAGE("del_dev_async not supported, skipping");
                }
            }
        }

        SUBCASE("get_queue_affinity") {
            cpu_set_t cpuset;
            ex::sync_wait(ex::starts_on(
                sched,
                ublk::get_queue_affinity(control_fd, dev_id, 0, &cpuset)));
            REQUIRE_THROWS(ex::sync_wait(ex::starts_on(
                sched,
                ublk::get_queue_affinity(control_fd, dev_id, 1, &cpuset))));
        }

        SUBCASE("get_dev_info") {
            ublksrv_ctrl_dev_info info_get = {};
            ex::sync_wait(ex::starts_on(
                sched, ublk::get_dev_info(control_fd, dev_id, &info_get)));
            REQUIRE(info_get.dev_id == dev_id);
            REQUIRE(info_get.nr_hw_queues == 1);
            REQUIRE(info_get.queue_depth == 32);
            REQUIRE(info_get.max_io_buf_bytes == (64u << 10));
            REQUIRE(info_get.state == UBLK_S_DEV_DEAD);
        }

        SUBCASE("set_params / get_params") {
            ublk_params params = {};
            fill_params(params);
            ex::sync_wait(ex::starts_on(
                sched, ublk::set_params(control_fd, dev_id, &params)));

            ublk_params params_get = {};
            params_get.len = sizeof(params_get);
            ex::sync_wait(ex::starts_on(
                sched, ublk::get_params(control_fd, dev_id, &params_get)));
            REQUIRE((params_get.types & UBLK_PARAM_TYPE_BASIC) != 0);
            REQUIRE(params_get.basic.dev_sectors == (64ull << 20) / 512);
            REQUIRE(params_get.basic.logical_bs_shift == 9);
            REQUIRE(params_get.basic.physical_bs_shift == 9);
            REQUIRE(params_get.basic.max_sectors == (64u << 10) / 512);

            SUBCASE("start_dev / stop_dev") {
                ublk::detail::IoLoop io_loop({}, 0, 0, {});
                auto io_sched = io_loop.get_scheduler();

                auto ublkc_path = ublk::detail::dev_path(dev_id);
                int ublkc_fd = open(ublkc_path.c_str(), O_RDWR);
                REQUIRE(ublkc_fd >= 0);
                auto d = ublk::detail::defer([&]() noexcept {
                    if (ublkc_fd >= 0) {
                        close(ublkc_fd);
                    }
                });

                ZeroHandler handler(info.flags);
                ex::simple_counting_scope scope;
                auto task = ublk::run_dev(ublkc_fd, 0, &info, handler);
                auto s =
                    ex::starts_on(io_sched, task) |
                    ex::upon_error([&](const std::exception_ptr &ep) noexcept {
                        REQUIRE(false);
                    });
                ex::spawn(s, scope.get_token());

                auto d2 = ublk::detail::defer(
                    [&]() noexcept { ex::sync_wait(scope.join()); });

                ex::sync_wait(ex::starts_on(
                    sched, ublk::start_dev(control_fd, dev_id, getpid())));

                auto d3 = ublk::detail::defer([&]() noexcept {
                    ex::sync_wait(ex::starts_on(
                        sched, ublk::stop_dev(control_fd, dev_id)));
                });

                if (unprivileged) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                auto ublkb_path = std::format("/dev/ublkb{}", dev_id);
                {
                    int ublkb_fd =
                        open(ublkb_path.c_str(), O_RDONLY | O_DIRECT);
                    REQUIRE(ublkb_fd >= 0);
                    auto d = ublk::detail::defer(
                        [&]() noexcept { close(ublkb_fd); });
                    alignas(4096) char buf[4096];
                    ssize_t n = pread(ublkb_fd, buf, sizeof(buf), 0);
                    REQUIRE(n == sizeof(buf));
                }

                SUBCASE("update_size") {
                    if (features & UBLK_F_UPDATE_SIZE) {
                        int ublkb_fd =
                            open(ublkb_path.c_str(), O_RDONLY | O_DIRECT);
                        REQUIRE(ublkb_fd >= 0);
                        auto d = ublk::detail::defer(
                            [&]() noexcept { close(ublkb_fd); });
                        alignas(4096) char buf[4096];

                        ssize_t n =
                            pread(ublkb_fd, buf, sizeof(buf), (64ull << 20));
                        REQUIRE(n == 0);

                        ex::sync_wait(ex::starts_on(
                            sched, ublk::update_size(control_fd, dev_id,
                                                     (128ull << 20) / 512)));

                        n = pread(ublkb_fd, buf, sizeof(buf), (64ull << 20));
                        REQUIRE(n == sizeof(buf));
                    } else {
                        MESSAGE("update_size not supported, skipping");
                    }
                }

                SUBCASE("register_shm_buf / unregister_shm_buf") {
                    if (features & UBLK_F_SHMEM_ZC) {
                        REQUIRE(handler.shm_hits() == 0);

                        const size_t shm_len = 4u << 20;
                        int shm_fd = memfd_create("ublk-test-shm", MFD_CLOEXEC);
                        REQUIRE(shm_fd >= 0);
                        auto d_shm_fd = ublk::detail::defer(
                            [&]() noexcept { close(shm_fd); });
                        REQUIRE(ftruncate(shm_fd,
                                          static_cast<off_t>(shm_len)) == 0);
                        void *shm =
                            mmap(nullptr, shm_len, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, shm_fd, 0);
                        REQUIRE(shm != MAP_FAILED);
                        auto d_shm = ublk::detail::defer(
                            [&]() noexcept { munmap(shm, shm_len); });

                        ublk_shmem_buf_reg reg = {};
                        reg.addr = reinterpret_cast<uint64_t>(shm);
                        reg.len = shm_len;
                        auto [index] =
                            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                            ex::sync_wait(
                                ex::starts_on(sched,
                                              ublk::register_shm_buf(
                                                  control_fd, dev_id, &reg)))
                                .value();
                        REQUIRE(index >= 0);
                        ex::sync_wait(
                            handler.handle_reg_shm(index, shm, shm_len));
                        auto d = ublk::detail::defer([&]() noexcept {
                            ex::sync_wait(ex::starts_on(
                                sched, ublk::unregister_shm_buf(
                                           control_fd, dev_id, index)));
                            handler.handle_unreg_shm(index);
                        });

                        {
                            int ublkb_fd =
                                open(ublkb_path.c_str(), O_RDONLY | O_DIRECT);
                            REQUIRE(ublkb_fd >= 0);
                            auto d = ublk::detail::defer(
                                [&]() noexcept { close(ublkb_fd); });
                            ssize_t n = pread(ublkb_fd, shm, 4096, 0);
                            REQUIRE(n == 4096);
                        }
                        REQUIRE(handler.shm_hits() >= 1);
                    } else {
                        MESSAGE("shmem zc not supported, skipping");
                    }
                }

                SUBCASE("try_stop_dev") {
                    if (features & UBLK_F_SAFE_STOP_DEV) {
                        bool ok = false;
                        for (size_t i = 0; i < 8; i++) {
                            auto s =
                                ublk::try_stop_dev(control_fd, dev_id) |
                                ex::then([]() noexcept { return true; }) |
                                ex::upon_error(
                                    [](const std::exception_ptr &) noexcept {
                                        return false;
                                    });
                            std::tie(ok) =
                                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                                ex::sync_wait(ex::starts_on(sched, s)).value();
                            if (ok) {
                                break;
                            }
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(50));
                        }
                        REQUIRE(ok);
                    } else {
                        MESSAGE("try_stop_dev not supported, skipping");
                    }
                }

                SUBCASE("quiesce_dev") {
                    if ((features & UBLK_F_QUIESCE) && !unprivileged) {
                        ex::sync_wait(ex::starts_on(
                            sched,
                            ublk::quiesce_dev(control_fd, dev_id, 1000)));

                        SUBCASE("start_user_recovery / end_user_recovery") {
                            close(ublkc_fd);
                            ublkc_fd = -1;
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(100));

                            ex::sync_wait(ex::starts_on(
                                sched,
                                ublk::start_user_recovery(control_fd, dev_id)));

                            ublkc_fd = open(ublkc_path.c_str(), O_RDWR);
                            REQUIRE(ublkc_fd >= 0);

                            auto task =
                                ublk::run_dev(ublkc_fd, 0, &info, handler);
                            auto s =
                                ex::starts_on(io_sched, task) |
                                ex::upon_error(
                                    [&](const std::exception_ptr &ep) noexcept {
                                        REQUIRE(false);
                                    });
                            ex::spawn(s, scope.get_token());

                            ex::sync_wait(ex::starts_on(
                                sched, ublk::end_user_recovery(
                                           control_fd, dev_id, getpid())));

                            {
                                int ublkb_fd = open(ublkb_path.c_str(),
                                                    O_RDONLY | O_DIRECT);
                                REQUIRE(ublkb_fd >= 0);
                                auto d = ublk::detail::defer(
                                    [&]() noexcept { close(ublkb_fd); });
                                alignas(4096) char buf[4096];
                                ssize_t n =
                                    pread(ublkb_fd, buf, sizeof(buf), 0);
                                REQUIRE(n == sizeof(buf));
                            }

                            ex::sync_wait(ex::starts_on(
                                sched, ublk::stop_dev(control_fd, dev_id)));
                        }
                    } else {
                        MESSAGE("quiesce_dev not supported, skipping");
                    }
                }
            }
        }
    }
}
