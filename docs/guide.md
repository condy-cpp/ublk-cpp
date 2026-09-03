# User Guide

@brief Step-by-step introduction to ublk-cpp's concepts and usage.

## Introduction

ublk is a kernel framework for implementing block device drivers in userspace. It exposes `/dev/ublkbN` block devices to upper-layer applications and forwards I/O requests coming from users to a userspace daemon via io_uring commands. The goal of **ublk-cpp** is to make building ublk daemons easy. It is built on top of [Condy](https://github.com/condy-cpp/condy) — a C++ asynchronous runtime based on io_uring. As a result, all ublk-cpp interfaces are exposed as **senders** from `std::execution`: they can be composed with the standard algorithms and coroutines, and interoperate directly with Condy's various asynchronous operations.

The runtime is represented by `condy::Runtime`, and a scheduler object can be obtained via `condy::get_scheduler()`. All interfaces provided by ublk-cpp are required to run on that scheduler.

```cpp
// sqe128 is required for ublk control cmd
condy::Runtime runtime(condy::RuntimeOptions().enable_sqe128());
std::jthread loop([&]() { runtime.run(); });
ex::scheduler auto sched = condy::get_scheduler(runtime);
ex::sync_wait(ex::starts_on(sched, ublk::add_dev(...)));
```

ublk-cpp lets you customize how a block device handles operations through handler callbacks: inside them you can build io_uring operation senders directly with the `condy::async_*` interfaces, writing asynchronous handling logic in a straightforward way.

```cpp
ex::task<int32_t> handle_io(const ublk::IoData&) noexcept {
    // ...
    co_await condy::async_read(...);
    // ...
}
```

The remainder of this guide walks through the features and interfaces ublk-cpp provides, one by one: the Control APIs of the control plane, I/O Handling, the High-Level Daemon APIs, followed by a complete server example and a few extra features.

## Control APIs

ublk-cpp wraps the ublk control commands into a set of APIs. For example, `ublk::add_dev()` takes a `ublksrv_ctrl_dev_info *info` and creates the corresponding ublk device based on the information in it. The ublk control commands can be grouped into the following categories.

- **Lifecycle management commands**: manage the ublk device — create, delete, start, stop, resume, etc. They include
    - `ublk::add_dev()`
    - `ublk::del_dev()`
    - `ublk::start_dev()`
    - `ublk::stop_dev()`
    - `ublk::set_params()`
    - `ublk::start_user_recovery()`
    - `ublk::end_user_recovery()`
    - `ublk::del_dev_async()`
    - `ublk::quiesce_dev()`
    - `ublk::try_stop_dev()`
- **Query commands**: query information about the ublk device. They include
    - `ublk::get_queue_affinity()`
    - `ublk::get_dev_info()`
    - `ublk::get_params()`
    - `ublk::get_features()`
- **Runtime control commands**: control ublk behavior at runtime. They include
    - `ublk::update_size()`
    - `ublk::register_shm_buf()`
    - `ublk::unregister_shm_buf()`

<!-- TODO: https://github.com/doxygen/doxygen/pull/12069 -->

## I/O Handling

Use the `ublk::run_dev()` function to run one ublk hardware queue. Requests submitted to that queue are handled by the custom logic you supply via the `handler` parameter. You can run this function on any `condy::Runtime`.

> [!NOTE]
> After `ublk::run_dev()` is invoked, it blocks until the device is stopped or an error occurs. Therefore, if you need to start the device on the same Runtime (e.g., via `ublk::start_dev()`), you must invoke `ublk::run_dev()` and `ublk::start_dev()` concurrently.

> [!NOTE]
> You don't necessarily have to use `ublk::run_dev()`. `ublk::daemon::run()` provides a higher-level interface.

The `handler` must satisfy the `ublk::IoHandler` concept. It should contain a sender factory named `handle_io()`. `handle_io()` takes a `const ublk::IoData&` as its argument, and the set_value path of the returned sender should produce either an `int32_t`, or a struct containing the fields `int32_t res;` and `uint64_t zone_lba;`.

```cpp
ex::task<int32_t> handle_io(const ublk::IoData&) noexcept; // ok

struct Result {
    int32_t res;
    uint64_t zone_lba;
};
ex::task<Result> handle_io(const ublk::IoData&) noexcept; // also ok
```

During operation, if the corresponding `condy::Runtime` has room for it, the passed-in `ublkc_fd` is automatically registered at position 0 of that Runtime's file table. Inside `handle_io()` you can use `condy::fixed()` to refer to this fd and perform various operations on it, e.g., `condy::async_read(condy::fixed(0), ...)`.

The `handler` may additionally define `init_queue(q_id)` and `destroy_queue(q_id)` callbacks to satisfy `ublk::QueueHandler`. If it satisfies that concept, the callbacks are invoked once per queue (`q_id` being the ID of the corresponding queue). You can perform scheduler-related initialization inside them, for instance registering an io_uring file table.

```cpp
struct Handler {
    auto init_queue(uint16_t q_id) noexcept {
        // Register backing fd to index 1
        return ex::just() | ex::then([]() noexcept {
            auto &fd_table = condy::current_runtime().fd_table();
            fd_table.update(BACKING_FD, &backing_fd, 1);
        });
    }

    auto handle_io(const ublk::IoData &io_data) noexcept { /* ... */ }

    void destroy_queue(uint16_t q_id) noexcept {
        // Unregister backing fd
        int fd = -1;
        auto &fd_table = condy::current_runtime().fd_table();
        fd_table.update(BACKING_FD, &fd, 1);
    }

    constexpr int BACKING_FD = 1;
    int backing_fd;
};
```

## High-Level Daemon APIs

On top of the control interfaces and the I/O handling interfaces, ublk-cpp provides a set of higher-level interfaces in `ublk::daemon`. They cover the lifecycle of a ublk daemon, hide device setup and recovery from the user, and present a uniform interface upward.

`ublk::daemon::setup()` takes a `ublksrv_ctrl_dev_info *info` as its argument. Its semantics: if the device does not exist, create the device described by `info` and return `true`; otherwise return `false`. Either way, the actual configuration of the device is written back into `info`. So as long as this function returns successfully, we are guaranteed the device exists and can be started by `ublk::daemon::start()`.

`ublk::daemon::configure()` takes a `ublk_params *params` as its argument. Its semantics: if the parameters can be set, configure the device according to `params` and return `true`; otherwise return `false`. Either way, the actual configuration of the device is written back into `params`. At that point, whether or not configuration actually took place, the daemon itself can determine its own behavior based on the `params` configuration.

`ublk::daemon::run()` is a higher-level wrapper around `ublk::run_dev()`. Internally it fetches the device information for `dev_id` on its own, configures the Runtime accordingly, and starts each hardware queue. You can tune the Runtime configuration via `ublk::daemon::Options`, though this is optional.

> [!NOTE]
> Inside `ublk::daemon::run()`, the `ublk::QueueHandler` callbacks can be used to perform per-queue initialization before I/O is actually processed.

`ublk::daemon::start()` starts the daemon. Internally it decides, based on the device state, whether to call `ublk::start_dev()` or `ublk::end_user_recovery()`.

> [!NOTE]
> Like `ublk::run_dev()`, `ublk::daemon::run()` blocks until the device is stopped or an error occurs. So you must again invoke `ublk::daemon::run()` and `ublk::daemon::start()` concurrently.

## A Complete Server

[ublk-nop](nop_8cpp.html) is a minimal, self-contained ublk server program implemented with ublk-cpp. You can run the server directly and stop it with a signal.

```console
$ sudo modprobe ublk_drv
$ sudo ./build/examples/ublk-nop -n 1
ublk-nop: ublk device 1 is running...
^Cublk-nop: received signal 2, shutting down...
```

Define the Runtime that sends control commands. sqe128 support must be enabled.

```cpp
// ...
condy::RuntimeOptions options;
options.enable_sqe128(); // or options.enable_sqe_mixed()
condy::Runtime runtime(options);
std::jthread loop([&]() { runtime.run(); });
auto d3 = ublk::detail::defer([&] noexcept { runtime.allow_exit(); });
ex::scheduler auto sched = condy::get_scheduler(runtime);
```

Query the features supported by the current kernel; if recovery is supported, add the corresponding flag. Then call `ublk::daemon::setup()` to make sure the device exists.

```cpp
uint64_t features;
ublksrv_ctrl_dev_info info;
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
    // ...
```

Define the `handler`. The parameters are prepared by `prep_params()` and applied by `ublk::daemon::configure()`.

```cpp
    // ...
    ex::let_value([&](bool) noexcept {
        prep_params(params, info);
        return ublk::daemon::configure(ctrl_fd, info.dev_id, &params);
    }) |
    // ...
```

Then run `ublk::daemon::run()` and `ublk::daemon::start()` concurrently. After `start()` completes, wait for a signal; if the signal arrives first, stop the current device with `ublk::stop_dev()`. If `run()` completes first, cancel the `wait_signal()` execution. Finally, once everything finishes, delete the device with `ublk::del_dev()`.

```cpp
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
// ...
struct {
    auto handle_io(const ublk::IoData &data) noexcept {
        return ex::just(data.iod->nr_sectors * 512);
    }
} handler;
static_assert(ublk::IoHandler<decltype(handler)>);
ex::inplace_stop_source stop_source;
// ...
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
```

## Other Features

### Shm Server

`ublk::register_shm_buf()` / `ublk::unregister_shm_buf()` enable zero-copy data exchange via shared memory between a ublk server and the processes that use the ublk block device. `ublk::daemon::run_shm_server()` provides a dynamic shared memory service over Unix sockets. Its protocol works like this:
1. A client connects to the server and passes an fd;
2. The server receives the fd, tries to map it as shared memory, and calls `ublk::register_shm_buf()`;
3. The server returns the registration result to the client as an `int32_t` value;
4. When the client closes the connection, the server calls `ublk::unregister_shm_buf()` and unmaps the memory.

`ublk::daemon::run_shm_server()` accepts a handler satisfying the `ublk::ShmHandler` concept, which consists of the `handle_reg_shm()` and `handle_unreg_shm()` callbacks. The former is called after `ublk::register_shm_buf()`; the latter is called after `ublk::unregister_shm_buf()`, at which point every request that depended on that shared memory has finished. In these two callbacks you can maintain the mapping between indexes and shm regions, so that any incoming shm requests can be handled.

### Unprivileged Mode

The control commands provided by ublk-cpp support unprivileged mode natively, and this is transparent to upper-layer users. You only need one set of code to deal with both modes. (Of course, you may still need udev configuration logic similar to what [ublksrv describes](https://github.com/ublk-org/ublksrv#unprivileged-mode), and use sleep and retry to handle the race conditions caused by permission changes. But there is only so much ublk-cpp can do.)

### Raw APIs

Under the `ublk::raw` namespace, ublk-cpp provides lightweight wrappers around the raw ublk operations (e.g., `ublk::raw::get_dev_info2()`). As with the higher-level interfaces, all of them are exposed as senders. If you want to implement ublk daemon logic in a freer way, these interfaces may come in handy.

### The ublkctl Tool

Like the ublk tool of ublksrv, ublkctl provides a command-line wrapper around a number of ublk control commands, but supports more operations. It can be used to manage ublk devices outside of a daemon — for example, moving operations such as `ublk::stop_dev()` out of the daemon.

### Environment Queries

ublk-cpp defines a query object named `ublk::fetch_dev_info`. Internally, ublk-cpp uses `ublk::get_dev_info()` by itself inside some interfaces to fetch device information, so as to present a more uniform interface upward. Since this is not on the critical path, it usually does not cause problems. But you may instead choose to set the `ublk::fetch_dev_info` environment via `ex::write_env()`; in that case, the various ublk-cpp commands will prefer the device information you provided. One possible use case looks like this:

```cpp
ublksrv_ctrl_dev_info info = {...};
auto s = ublk::add_dev(ctrl_fd, &info) | ex::let_value([&]() noexcept {
             return ublk::set_params(ctrl_fd, info.dev_id, params);
         }) |
         // ...
         ex::let_value([&]() noexcept {
             return ublk::del_dev(ctrl_fd, info.dev_id);
         }) |
         ex::write_env(ex::prop{ublk::fetch_dev_info, &info});
```
