#include "helpers.hpp"
#include <concepts>
#include <condy.hpp>
#include <doctest.h>
#include <fcntl.h>
#include <memory>
#include <thread>
#include <ublk.hpp>
#include <unistd.h>

namespace ex = condy::detail::ex;

TEST_CASE("test query - fetch_dev_info defaults to nullptr") {
    ex::env empty{};
    const ublksrv_ctrl_dev_info *info = ublk::fetch_dev_info(empty);
    REQUIRE(info == nullptr);

    static_assert(std::same_as<decltype(ublk::fetch_dev_info(ex::env{})),
                               const ublksrv_ctrl_dev_info *>);
}

TEST_CASE("test query - fetch_dev_info forwards the env query") {
    ublksrv_ctrl_dev_info stored{};
    stored.dev_id = 42;

    auto p = ex::prop{ublk::fetch_dev_info, &stored};
    const ublksrv_ctrl_dev_info *info = ublk::fetch_dev_info(p);
    REQUIRE(info == &stored);
    REQUIRE(info->dev_id == 42);
}

TEST_CASE("test query - cached_get_dev_info cache hit") {
    ublk::detail::IoLoop loop(condy::RuntimeOptions().enable_sqe128(), 0, 0,
                              {});
    auto sched = loop.get_scheduler();

    ublksrv_ctrl_dev_info cached{};
    cached.dev_id = 1024;
    cached.nr_hw_queues = 4;
    cached.queue_depth = 32;
    cached.state = UBLK_S_DEV_DEAD;

    ublksrv_ctrl_dev_info out{};
    auto s =
        ex::starts_on(
            sched, ublk::detail::cached_get_dev_info<decltype(sched),
                                                     std::allocator<std::byte>>(
                       -1, cached.dev_id, &out)) |
        ex::write_env(ex::prop{ublk::fetch_dev_info, &cached});
    ex::sync_wait(std::move(s));

    // fd = -1 would fail if a syscall were issued; a cache hit avoids it.
    REQUIRE(out.dev_id == cached.dev_id);
    REQUIRE(out.nr_hw_queues == cached.nr_hw_queues);
    REQUIRE(out.queue_depth == cached.queue_depth);
    REQUIRE(out.state == cached.state);
}

TEST_CASE("test query - cached_get_dev_info cache miss") {
    ublk::detail::IoLoop loop(condy::RuntimeOptions().enable_sqe128(), 0, 0,
                              {});
    auto sched = loop.get_scheduler();

    int control_fd = open("/dev/ublk-control", O_RDWR);
    REQUIRE(control_fd >= 0);
    auto d = ublk::detail::defer([&]() noexcept { close(control_fd); });

    bool unprivileged = !check_privileged();
    uint32_t dev_id = -1;
    ublksrv_ctrl_dev_info info = {};
    fill_dev_info(info, dev_id, 1, unprivileged ? UBLK_F_UNPRIVILEGED_DEV : 0);
    ex::sync_wait(ex::starts_on(sched, ublk::add_dev(control_fd, &info)));
    dev_id = info.dev_id;
    auto d2 = ublk::detail::defer([&]() noexcept {
        ex::sync_wait(ex::starts_on(sched, ublk::del_dev(control_fd, dev_id)));
    });
    if (unprivileged) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // No cached info in the env -> miss -> fetch via syscall.
    ublksrv_ctrl_dev_info out{};
    auto s = ex::starts_on(
        sched, ublk::detail::cached_get_dev_info<decltype(sched),
                                                 std::allocator<std::byte>>(
                   control_fd, dev_id, &out));
    ex::sync_wait(std::move(s));
    REQUIRE(out.dev_id == dev_id);
    REQUIRE(out.nr_hw_queues == 1);

    // Cached info for a different dev_id -> miss -> fetch via syscall.
    ublksrv_ctrl_dev_info stale{};
    stale.dev_id = dev_id + 1;
    stale.nr_hw_queues = 99;
    ublksrv_ctrl_dev_info out2{};
    auto s2 =
        ex::starts_on(
            sched, ublk::detail::cached_get_dev_info<decltype(sched),
                                                     std::allocator<std::byte>>(
                       control_fd, dev_id, &out2)) |
        ex::write_env(ex::prop{ublk::fetch_dev_info, &stale});
    ex::sync_wait(std::move(s2));
    REQUIRE(out2.dev_id == dev_id);
    REQUIRE(out2.nr_hw_queues == 1);
}
