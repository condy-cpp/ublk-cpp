#include "helpers.hpp"
#include <chrono>
#include <condy.hpp>
#include <doctest.h>
#include <fcntl.h>
#include <thread>
#include <ublk.hpp>
#include <unistd.h>

namespace ex = condy::detail::ex;

TEST_CASE("test io") {
    ublk::detail::IoLoop loop(condy::RuntimeOptions().enable_sqe128(), 0, 32,
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

    SUBCASE("arguments validation") {
        if (!unprivileged && (features & UBLK_F_SUPPORT_ZERO_COPY)) {
            ublk::detail::IoLoop io_loop({}, 0, 16, {});
            auto io_sched = io_loop.get_scheduler();

            uint32_t dev_id = -1;
            ublksrv_ctrl_dev_info info = {};
            fill_dev_info(info, dev_id, 1, UBLK_F_SUPPORT_ZERO_COPY);
            ex::sync_wait(
                ex::starts_on(sched, ublk::add_dev(control_fd, &info)));
            dev_id = info.dev_id;
            auto d = ublk::detail::defer([&]() noexcept {
                ex::sync_wait(
                    ex::starts_on(sched, ublk::del_dev(control_fd, dev_id)));
            });

            auto ublkc_path = ublk::detail::dev_path(dev_id);
            int ublkc_fd = open(ublkc_path.c_str(), O_RDWR);
            REQUIRE(ublkc_fd >= 0);
            auto d2 = ublk::detail::defer([&]() noexcept { close(ublkc_fd); });

            ZeroHandler handler(info.flags);

            SUBCASE("q_id out of range") {
                REQUIRE_THROWS(ex::sync_wait(ex::starts_on(
                    io_sched, ublk::run_dev(ublkc_fd, 1, &info, handler))));
            }

            SUBCASE("buffer table too small") {
                REQUIRE_THROWS(ex::sync_wait(ex::starts_on(
                    io_sched, ublk::run_dev(ublkc_fd, 0, &info, handler))));
            }
        } else {
            MESSAGE("zero copy not supported, skipping");
        }
    }

    SUBCASE("run io") {
        ublk::detail::IoLoop io_loop({}, 1, 32, {});
        auto io_sched = io_loop.get_scheduler();

        auto run_io = [&](uint64_t flags) {
            uint32_t dev_id = -1;
            ublksrv_ctrl_dev_info info = {};
            uint64_t dev_flags =
                unprivileged ? UBLK_F_UNPRIVILEGED_DEV | (flags & features)
                             : flags & features;
            fill_dev_info(info, dev_id, 1, dev_flags);
            ex::sync_wait(
                ex::starts_on(sched, ublk::add_dev(control_fd, &info)));
            dev_id = info.dev_id;
            auto d = ublk::detail::defer([&]() noexcept {
                ex::sync_wait(
                    ex::starts_on(sched, ublk::del_dev(control_fd, dev_id)));
            });
            if (unprivileged) {
                // Wait for udev to chown /dev/ublkcN.
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            ublk_params params = {};
            fill_params(params);
            ex::sync_wait(ex::starts_on(
                sched, ublk::set_params(control_fd, dev_id, &params)));

            auto ublkc_path = ublk::detail::dev_path(dev_id);
            int ublkc_fd = open(ublkc_path.c_str(), O_RDWR);
            REQUIRE(ublkc_fd >= 0);
            auto d2 = ublk::detail::defer([&]() noexcept { close(ublkc_fd); });

            ZeroHandler handler(info.flags);
            ex::simple_counting_scope scope;
            auto task = ublk::run_dev(ublkc_fd, 0, &info, handler);
            auto s = ex::starts_on(io_sched, task) |
                     ex::upon_error([&](const std::exception_ptr &ep) noexcept {
                         REQUIRE(false);
                     });
            ex::spawn(s, scope.get_token());

            auto d3 = ublk::detail::defer(
                [&]() noexcept { ex::sync_wait(scope.join()); });

            ex::sync_wait(ex::starts_on(
                sched, ublk::start_dev(control_fd, dev_id, getpid())));

            if (unprivileged) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            auto d4 = ublk::detail::defer([&]() noexcept {
                ex::sync_wait(
                    ex::starts_on(sched, ublk::stop_dev(control_fd, dev_id)));
            });

            auto ublkb_path = std::format("/dev/ublkb{}", dev_id);
            {
                int ublkb_fd = open(ublkb_path.c_str(), O_RDWR | O_DIRECT);
                REQUIRE(ublkb_fd >= 0);
                auto d =
                    ublk::detail::defer([&]() noexcept { close(ublkb_fd); });
                alignas(4096) char buf[4096];
                ssize_t n = pread(ublkb_fd, buf, sizeof(buf), 0);
                REQUIRE(n == sizeof(buf));

                n = pwrite(ublkb_fd, buf, sizeof(buf), 0);
                REQUIRE(n == sizeof(buf));
            }
        };

        SUBCASE("basic") { run_io(0); }

        SUBCASE("zero copy") {
            if (!unprivileged && (features & UBLK_F_SUPPORT_ZERO_COPY)) {
                run_io(UBLK_F_SUPPORT_ZERO_COPY);
            } else {
                MESSAGE("zero copy not supported, skipping");
            }
        }

        SUBCASE("user copy") {
            if (!unprivileged && (features & UBLK_F_USER_COPY)) {
                run_io(UBLK_F_USER_COPY);
            } else {
                MESSAGE("user copy not supported, skipping");
            }
        }

        SUBCASE("need get data") {
            if (features & UBLK_F_NEED_GET_DATA) {
                run_io(UBLK_F_NEED_GET_DATA);
            } else {
                MESSAGE("need get data not supported, skipping");
            }
        }

        SUBCASE("zero copy auto reg") {
            if (!unprivileged && (features & UBLK_F_SUPPORT_ZERO_COPY) &&
                (features & UBLK_F_AUTO_BUF_REG)) {
                run_io(UBLK_F_SUPPORT_ZERO_COPY | UBLK_F_AUTO_BUF_REG);
            } else {
                MESSAGE("zero copy auto reg not supported, skipping");
            }
        }

        SUBCASE("batch io") {
            if (features & UBLK_F_BATCH_IO) {
                run_io(UBLK_F_BATCH_IO);
            } else {
                MESSAGE("batch io not supported, skipping");
            }
        }

        SUBCASE("batch io + user copy") {
            if (!unprivileged && (features & UBLK_F_USER_COPY) &&
                (features & UBLK_F_BATCH_IO)) {
                run_io(UBLK_F_USER_COPY | UBLK_F_BATCH_IO);
            } else {
                MESSAGE("batch io + user copy not supported, skipping");
            }
        }

        SUBCASE("batch io + need get data") {
            if ((features & UBLK_F_NEED_GET_DATA) &&
                (features & UBLK_F_BATCH_IO)) {
                run_io(UBLK_F_NEED_GET_DATA | UBLK_F_BATCH_IO);
            } else {
                MESSAGE("batch io + need get data not supported, skipping");
            }
        }

        SUBCASE("batch io + zero copy") {
            if (!unprivileged && (features & UBLK_F_SUPPORT_ZERO_COPY) &&
                (features & UBLK_F_BATCH_IO)) {
                run_io(UBLK_F_SUPPORT_ZERO_COPY | UBLK_F_BATCH_IO);
            } else {
                MESSAGE("batch io + zero copy not supported, skipping");
            }
        }

        SUBCASE("batch io + zero copy auto reg") {
            if (!unprivileged && (features & UBLK_F_SUPPORT_ZERO_COPY) &&
                (features & UBLK_F_AUTO_BUF_REG) &&
                (features & UBLK_F_BATCH_IO)) {
                run_io(UBLK_F_SUPPORT_ZERO_COPY | UBLK_F_AUTO_BUF_REG |
                       UBLK_F_BATCH_IO);
            } else {
                MESSAGE(
                    "batch io + zero copy auto reg not supported, skipping");
            }
        }
    }

    SUBCASE("run io with error") {
        ublk::detail::IoLoop io_loop({}, 1, 32, {});
        auto io_sched = io_loop.get_scheduler();

        auto run_error_io = [&](auto fn, int expected) {
            uint32_t dev_id = -1;
            ublksrv_ctrl_dev_info info = {};
            uint64_t dev_flags = unprivileged ? UBLK_F_UNPRIVILEGED_DEV : 0;
            fill_dev_info(info, dev_id, 1, dev_flags);
            ex::sync_wait(
                ex::starts_on(sched, ublk::add_dev(control_fd, &info)));
            dev_id = info.dev_id;
            auto d = ublk::detail::defer([&]() noexcept {
                ex::sync_wait(ex::starts_on(
                    sched,
                    ublk::del_dev(control_fd, dev_id) |
                        ex::upon_error(
                            [](const std::exception_ptr &) noexcept {})));
            });
            if (unprivileged) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            ublk_params params = {};
            fill_params(params);
            ex::sync_wait(ex::starts_on(
                sched, ublk::set_params(control_fd, dev_id, &params)));

            auto ublkc_path = ublk::detail::dev_path(dev_id);
            int ublkc_fd = open(ublkc_path.c_str(), O_RDWR);
            REQUIRE(ublkc_fd >= 0);
            auto d2 = ublk::detail::defer([&]() noexcept { close(ublkc_fd); });

            struct {
                decltype(fn) fn_;
                auto handle_io(const ublk::IoData &) noexcept { return fn_(); };
            } handler{fn};
            ex::simple_counting_scope scope;
            auto task = ublk::run_dev(ublkc_fd, 0, &info, handler);
            auto s = ex::starts_on(io_sched, std::move(task)) |
                     ex::upon_error([&](const std::exception_ptr &ep) noexcept {
                         REQUIRE(false);
                     });
            ex::spawn(std::move(s), scope.get_token());

            auto d3 = ublk::detail::defer(
                [&]() noexcept { ex::sync_wait(scope.join()); });

            ex::sync_wait(ex::starts_on(
                sched, ublk::start_dev(control_fd, dev_id, getpid())));

            if (unprivileged) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            auto d4 = ublk::detail::defer([&]() noexcept {
                ex::sync_wait(
                    ex::starts_on(sched, ublk::stop_dev(control_fd, dev_id)));
            });

            auto ublkb_path = std::format("/dev/ublkb{}", dev_id);
            {
                int ublkb_fd = open(ublkb_path.c_str(), O_RDONLY | O_DIRECT);
                REQUIRE(ublkb_fd >= 0);
                auto d =
                    ublk::detail::defer([&]() noexcept { close(ublkb_fd); });
                alignas(4096) char buf[4096];
                ssize_t n = pread(ublkb_fd, buf, sizeof(buf), 0);
                REQUIRE(n == -1);
                REQUIRE(errno == expected);
            }
        };

        SUBCASE("return error") {
            run_error_io([]() noexcept { return ex::just(-EIO); }, EIO);
        }

        SUBCASE("error via error_code") {
            run_error_io(
                []() noexcept {
                    return ex::just_error(
                        std::error_code(EIO, std::generic_category()));
                },
                EIO);
        }

        SUBCASE("error via system_error") {
            run_error_io(
                []() noexcept {
                    return ex::just_error(std::make_exception_ptr(
                        std::system_error(ENOSPC, std::generic_category())));
                },
                ENOSPC);
        }

        SUBCASE("error via other exception") {
            run_error_io(
                []() noexcept {
                    return ex::just_error(
                        std::make_exception_ptr(std::runtime_error("boom")));
                },
                EIO);
        }
    }
}