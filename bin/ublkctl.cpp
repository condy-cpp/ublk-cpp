/**
 * @file ublkctl.cpp
 * @brief Command line tool to control ublk block devices.
 */

#include <CLI11.hpp>
#include <cerrno>
#include <charconv>
#include <condy.hpp>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <print>
#include <sched.h>
#include <string>
#include <ublk.hpp>
#include <unistd.h>
#include <utility>
#include <variant>

namespace ex = condy::detail::ex;

struct AddCmd {
    uint32_t dev_id = -1;
    uint16_t nr_queues = 1;
    uint16_t queue_depth = 32;
    uint32_t max_io_buf_bytes = 0;
    uint64_t flags = 0;
};

struct DelCmd {
    uint32_t dev_id;
    bool async = false;
};

struct StartCmd {
    uint32_t dev_id;
    int32_t daemon_pid;
};

struct StopCmd {
    uint32_t dev_id;
    bool try_stop = false;
};

struct ListCmd {};

struct GetCmd {
    uint32_t dev_id;
};

struct StartRecoveryCmd {
    uint32_t dev_id;
};

struct RecoverCmd {
    uint32_t dev_id;
    int32_t daemon_pid;
};

struct FeaturesCmd {};

struct QuiesceCmd {
    uint32_t dev_id;
    uint64_t timeout_ms;
};

using Cmd = std::variant<AddCmd, DelCmd, StartCmd, StopCmd, ListCmd, GetCmd,
                         StartRecoveryCmd, RecoverCmd, FeaturesCmd, QuiesceCmd>;

const char *state_str(uint32_t state) noexcept {
    switch (state) {
    case UBLK_S_DEV_DEAD:
        return "DEAD";
    case UBLK_S_DEV_LIVE:
        return "LIVE";
    case UBLK_S_DEV_QUIESCED:
        return "QUIESCED";
    case UBLK_S_DEV_FAIL_IO:
        return "FAIL_IO";
    default:
        return "UNKNOWN";
    }
}

constexpr struct {
    uint64_t flag;
    const char *name;
    const char *short_name;
} FLAG_TABLE[] = {
    {UBLK_F_SUPPORT_ZERO_COPY, "SUPPORT_ZERO_COPY", "ZERO_COPY"},
    {UBLK_F_URING_CMD_COMP_IN_TASK, "URING_CMD_COMP_IN_TASK", "COMP_IN_TASK"},
    {UBLK_F_NEED_GET_DATA, "NEED_GET_DATA", "GET_DATA"},
    {UBLK_F_USER_RECOVERY, "USER_RECOVERY", "USER_RECOVERY"},
    {UBLK_F_USER_RECOVERY_REISSUE, "USER_RECOVERY_REISSUE", "RECOVERY_REISSUE"},
    {UBLK_F_UNPRIVILEGED_DEV, "UNPRIVILEGED_DEV", "UNPRIVILEGED_DEV"},
    {UBLK_F_CMD_IOCTL_ENCODE, "CMD_IOCTL_ENCODE", "CMD_IOCTL_ENCODE"},
    {UBLK_F_USER_COPY, "USER_COPY", "USER_COPY"},
    {UBLK_F_ZONED, "ZONED", "ZONED"},
    {UBLK_F_USER_RECOVERY_FAIL_IO, "USER_RECOVERY_FAIL_IO", "RECOVERY_FAIL_IO"},
    {UBLK_F_UPDATE_SIZE, "UPDATE_SIZE", "UPDATE_SIZE"},
    {UBLK_F_AUTO_BUF_REG, "AUTO_BUF_REG", "AUTO_ZC"},
    {UBLK_F_QUIESCE, "QUIESCE", "QUIESCE"},
    {UBLK_F_PER_IO_DAEMON, "PER_IO_DAEMON", "PER_IO_DAEMON"},
    {UBLK_F_BUF_REG_OFF_DAEMON, "BUF_REG_OFF_DAEMON", "BUF_REG_OFF_DAEMON"},
    {UBLK_F_BATCH_IO, "BATCH_IO", "BATCH_IO"},
    {UBLK_F_INTEGRITY, "INTEGRITY", "INTEGRITY"},
    {UBLK_F_SAFE_STOP_DEV, "SAFE_STOP_DEV", "SAFE_STOP_DEV"},
    {UBLK_F_NO_AUTO_PART_SCAN, "NO_AUTO_PART_SCAN", "NO_AUTO_PART_SCAN"},
    {UBLK_F_SHMEM_ZC, "SHMEM_ZC", "SHMEM_ZC"},
};

std::string flags_str(uint64_t flags) noexcept {
    std::string s;
    for (const auto &e : FLAG_TABLE) {
        if (flags & e.flag) {
            if (!s.empty()) {
                s += ' ';
            }
            s += e.name;
        }
    }
    return s;
}

void dump_dev_info(uint32_t dev_id, const ublksrv_ctrl_dev_info &info,
                   const ublk_params &params) {
    std::println("dev id {}: nr_hw_queues {} queue_depth {} block size {} "
                 "dev_capacity {}",
                 dev_id, info.nr_hw_queues, info.queue_depth,
                 1U << params.basic.logical_bs_shift, params.basic.dev_sectors);
    std::println("\tmax rq size {} daemon pid {} state {}",
                 info.max_io_buf_bytes, info.ublksrv_pid,
                 state_str(info.state));
    std::println("\tflags 0x{:x} [{}]", info.flags, flags_str(info.flags));
    std::println("\tublkc: {}:{} ublkb: {}:{} owner: {}:{}",
                 params.devt.char_major, params.devt.char_minor,
                 params.devt.disk_major, params.devt.disk_minor, info.owner_uid,
                 info.owner_gid);
}

std::string cpu_str(const cpu_set_t &cpuset) noexcept {
    std::string s;
    size_t i = 0;
    while (i < CPU_SETSIZE) {
        if (!CPU_ISSET(i, &cpuset)) {
            i++;
            continue;
        }
        size_t start = i;
        do {
            i++;
        } while (i < CPU_SETSIZE && CPU_ISSET(i, &cpuset));
        size_t end = i - 1;
        if (!s.empty()) {
            s += ' ';
        }
        s += std::to_string(start);
        if (end > start) {
            s += '-';
            s += std::to_string(end);
        }
    }
    return s;
}

ex::task<void> dump_queue_affinity(int ctrl_fd,
                                   const ublksrv_ctrl_dev_info &info) {
    for (uint16_t q_id = 0; q_id < info.nr_hw_queues; q_id++) {
        cpu_set_t cpuset;
        co_await ublk::get_queue_affinity(ctrl_fd, info.dev_id, q_id, &cpuset);
        std::println("\tqueue {}: affinity({})", q_id, cpu_str(cpuset));
    }
}

ex::task<void> run_cmd(int ctrl_fd, const AddCmd &cmd) {
    ublksrv_ctrl_dev_info info = {};
    info.dev_id = cmd.dev_id;
    info.nr_hw_queues = cmd.nr_queues;
    info.queue_depth = cmd.queue_depth;
    info.max_io_buf_bytes = cmd.max_io_buf_bytes;
    info.flags = cmd.flags;
    co_await ublk::add_dev(ctrl_fd, &info);
    ublk_params params = {};
    params.len = sizeof(params);
    co_await ublk::get_params(ctrl_fd, info.dev_id, &params);
    dump_dev_info(info.dev_id, info, params);
    co_await dump_queue_affinity(ctrl_fd, info);
}

ex::task<void> run_cmd(int ctrl_fd, const DelCmd &cmd) {
    if (cmd.async) {
        co_await ublk::del_dev_async(ctrl_fd, cmd.dev_id);
    } else {
        co_await ublk::del_dev(ctrl_fd, cmd.dev_id);
    }
}

ex::task<void> run_cmd(int ctrl_fd, const StartCmd &cmd) {
    co_await ublk::start_dev(ctrl_fd, cmd.dev_id, cmd.daemon_pid);
}

ex::task<void> run_cmd(int ctrl_fd, const StopCmd &cmd) {
    if (cmd.try_stop) {
        co_await ublk::try_stop_dev(ctrl_fd, cmd.dev_id);
    } else {
        co_await ublk::stop_dev(ctrl_fd, cmd.dev_id);
    }
}

ex::task<void> run_cmd(int ctrl_fd, const ListCmd &) {
    for (auto &entry :
         std::filesystem::directory_iterator("/sys/class/ublk-char")) {
        const std::string name = entry.path().filename().string();
        if (!name.starts_with("ublkc"))
            continue;
        size_t id = 0;
        auto [ptr, ec] =
            std::from_chars(name.data() + 5, name.data() + name.size(), id);
        if (ec != std::errc{} || ptr != name.data() + name.size())
            continue;
        ublksrv_ctrl_dev_info info = {};
        co_await ublk::get_dev_info(ctrl_fd, id, &info);
        ublk_params params = {};
        params.len = sizeof(params);
        co_await ublk::get_params(ctrl_fd, id, &params);
        dump_dev_info(id, info, params);
        co_await dump_queue_affinity(ctrl_fd, info);
    }
}

ex::task<void> run_cmd(int ctrl_fd, const GetCmd &cmd) {
    ublksrv_ctrl_dev_info info = {};
    co_await ublk::get_dev_info(ctrl_fd, cmd.dev_id, &info);
    ublk_params params = {};
    params.len = sizeof(params);
    co_await ublk::get_params(ctrl_fd, cmd.dev_id, &params);
    dump_dev_info(cmd.dev_id, info, params);
    co_await dump_queue_affinity(ctrl_fd, info);
}

ex::task<void> run_cmd(int ctrl_fd, const StartRecoveryCmd &cmd) {
    co_await ublk::start_user_recovery(ctrl_fd, cmd.dev_id);
}

ex::task<void> run_cmd(int ctrl_fd, const RecoverCmd &cmd) {
    co_await ublk::end_user_recovery(ctrl_fd, cmd.dev_id, cmd.daemon_pid);
}

ex::task<void> run_cmd(int ctrl_fd, const FeaturesCmd &) {
    uint64_t features = 0;
    co_await ublk::get_features(ctrl_fd, &features);

    std::println("ublk_drv features: 0x{:x}", features);
    for (const auto &e : FLAG_TABLE) {
        if (features & e.flag)
            std::println("\t{:<20s}: 0x{:x}", e.short_name, e.flag);
    }
}

ex::task<void> run_cmd(int ctrl_fd, const QuiesceCmd &cmd) {
    co_await ublk::quiesce_dev(ctrl_fd, cmd.dev_id, cmd.timeout_ms);
}

Cmd parse_args(int argc, char *argv[]) {
    CLI::App app{"ublk userspace block device control tool"};

    AddCmd add_opts;
    DelCmd del_opts;
    StartCmd start_opts;
    StopCmd stop_opts;
    GetCmd get_opts;
    StartRecoveryCmd start_recovery_opts;
    RecoverCmd recover_opts;
    QuiesceCmd quiesce_opts;

    app.require_subcommand();

    auto *add_cmd = app.add_subcommand("add", "add a device");
    add_cmd->add_option("-n", add_opts.dev_id, "device id (default: auto)");
    add_cmd->add_option("-q", add_opts.nr_queues, "number of queues");
    add_cmd->add_option("-d", add_opts.queue_depth, "queue depth");
    add_cmd->add_option("-b", add_opts.max_io_buf_bytes, "max io buf bytes");
    add_cmd->add_flag_callback("--zerocopy", [&add_opts]() {
        add_opts.flags |= UBLK_F_SUPPORT_ZERO_COPY;
    });
    add_cmd->add_flag_callback(
        "--batch", [&add_opts]() { add_opts.flags |= UBLK_F_BATCH_IO; });
    add_cmd->add_flag_callback("--user-recovery", [&add_opts]() {
        add_opts.flags |= UBLK_F_USER_RECOVERY;
    });
    add_cmd->add_flag_callback("--user-recovery-fail-io", [&add_opts]() {
        add_opts.flags |= UBLK_F_USER_RECOVERY_FAIL_IO;
    });
    add_cmd->add_flag_callback("--user-recovery-reissue", [&add_opts]() {
        add_opts.flags |= UBLK_F_USER_RECOVERY_REISSUE;
    });
    add_cmd->add_flag_callback("--unprivileged", [&add_opts]() {
        add_opts.flags |= UBLK_F_UNPRIVILEGED_DEV;
    });
    add_cmd->add_flag_callback(
        "--usercopy", [&add_opts]() { add_opts.flags |= UBLK_F_USER_COPY; });

    auto *del_cmd = app.add_subcommand("del", "delete a device");
    del_cmd->add_option("-n", del_opts.dev_id, "device id")->required();
    del_cmd->add_flag("--async", del_opts.async, "delete asynchronously");

    auto *start_cmd = app.add_subcommand("start", "start a device");
    start_cmd->add_option("-n", start_opts.dev_id, "device id")->required();
    start_cmd->add_option("--pid", start_opts.daemon_pid, "daemon pid")
        ->required();

    auto *stop_cmd = app.add_subcommand("stop", "stop a device");
    stop_cmd->add_option("-n", stop_opts.dev_id, "device id")->required();
    stop_cmd->add_flag("--try", stop_opts.try_stop, "try stop");

    app.add_subcommand("list", "list devices");

    auto *get_cmd = app.add_subcommand("get", "get device info");
    get_cmd->add_option("-n", get_opts.dev_id, "device id")->required();

    auto *start_recovery_cmd =
        app.add_subcommand("start-recovery", "start user recovery");
    start_recovery_cmd
        ->add_option("-n", start_recovery_opts.dev_id, "device id")
        ->required();

    auto *recover_cmd = app.add_subcommand("recover", "recover a device");
    recover_cmd->add_option("-n", recover_opts.dev_id, "device id")->required();
    recover_cmd->add_option("--pid", recover_opts.daemon_pid, "daemon pid")
        ->required();

    app.add_subcommand("features", "get device features");

    auto *quiesce_cmd = app.add_subcommand("quiesce", "quiesce a device");
    quiesce_cmd->add_option("-n", quiesce_opts.dev_id, "device id")->required();
    quiesce_cmd
        ->add_option("timeout_ms", quiesce_opts.timeout_ms, "timeout in ms")
        ->required();

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        std::exit(app.exit(e));
    }

    if (app.got_subcommand("add")) {
        return add_opts;
    } else if (app.got_subcommand("del")) {
        return del_opts;
    } else if (app.got_subcommand("start")) {
        return start_opts;
    } else if (app.got_subcommand("stop")) {
        return stop_opts;
    } else if (app.got_subcommand("list")) {
        return ListCmd{};
    } else if (app.got_subcommand("get")) {
        return get_opts;
    } else if (app.got_subcommand("start-recovery")) {
        return start_recovery_opts;
    } else if (app.got_subcommand("recover")) {
        return recover_opts;
    } else if (app.got_subcommand("features")) {
        return FeaturesCmd{};
    } else if (app.got_subcommand("quiesce")) {
        return quiesce_opts;
    } else {
        std::unreachable();
    }
}

// NOLINTNEXTLINE(bugprone-unsafe-to-allow-exceptions)
int main(int argc, char *argv[]) noexcept(false) {
    try {
        auto cmd = parse_args(argc, argv);

        int ctrl_fd = open("/dev/ublk-control", O_RDWR);
        if (ctrl_fd < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "open /dev/ublk-control");
        }
        auto d = ublk::detail::defer([&]() noexcept { close(ctrl_fd); });

        ublk::detail::IoLoop loop(condy::RuntimeOptions().enable_sqe128(), 0, 0,
                                  {});
        ex::sync_wait(ex::starts_on(
            loop.get_scheduler(),
            // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
            std::visit([&](const auto &c) { return run_cmd(ctrl_fd, c); },
                       cmd)));
    } catch (const std::system_error &e) {
        std::println(std::cerr, "ublkctl: {}", e.what());
        return e.code().value();
    } catch (const std::exception &e) {
        std::println(std::cerr, "ublkctl: {}", e.what());
        return 1;
    }
    return 0;
}
