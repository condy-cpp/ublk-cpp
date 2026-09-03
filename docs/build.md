# Building and Usage

@brief How to build and integrate ublk-cpp in your project.

> [!NOTE]
> ublk-cpp currently depends on condy's **experimental execution integration**, which can be backed by either **stdexec** (default) or **beman/execution**. Once `std::execution` is finalized in C++26, it will migrate to the standard library implementation.

## Using ublk-cpp as a Submodule

You can add ublk-cpp to your project via Git submodule:

```bash
git submodule add https://github.com/condy-cpp/ublk-cpp.git third_party/ublk-cpp
git submodule update --init --recursive
```

In your `CMakeLists.txt`:

```cmake
add_subdirectory(third_party/ublk-cpp)
add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE ublkcpp)
```

## Using ublk-cpp via FetchContent

Alternatively, you can add ublk-cpp with FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(
    ublk-cpp
    GIT_REPOSITORY https://github.com/condy-cpp/ublk-cpp.git
    GIT_TAG master  # Change to the commit/tag you want
)
FetchContent_MakeAvailable(ublk-cpp)
add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE ublkcpp)
```

## Dependencies

Condy fetches and **statically links liburing** by default (`CONDY_LINK_LIBURING=ON`). To use the liburing installed on your system instead, configure with `CONDY_LINK_LIBURING=OFF`.

ublk-cpp currently also fetches and depends on **[stdexec](https://github.com/NVIDIA/stdexec)**, or **[beman/execution](https://github.com/bemanproject/execution)** when enabled via the `UBLKCPP_EXECUTION_BACKEND` option. Once `std::execution` is finalized in C++26, this dependency is expected to be replaced by the standard library implementation.

## Building

ublk-cpp provides CMake options to build tests, the `ublkctl` tool, examples, and the Doxygen documentation:

| Option | Description | Default |
| --- | --- | --- |
| `UBLKCPP_BUILD_TESTS` | Build tests | OFF |
| `UBLKCPP_BUILD_UBLKCTL` | Build the `ublkctl` tool | OFF |
| `UBLKCPP_BUILD_EXAMPLES` | Build examples | OFF |
| `UBLKCPP_BUILD_DOCS` | Build Doxygen documentation | OFF |
| `UBLKCPP_USE_URING_CMD128` | Use `IORING_OP_URING_CMD128` for control commands | ON |
| `UBLKCPP_TESTS_STATIC_LINK` | Use static linking for tests | OFF |
| `UBLKCPP_TESTS_ASAN` | Enable ASan/UBSan for tests | OFF |
| `UBLKCPP_EXECUTION_BACKEND` | `std::execution` implementation used via Condy: `stdexec` or `beman` | `stdexec` |

```bash
cmake -B build -S . \
    -DUBLKCPP_BUILD_TESTS=ON \
    -DUBLKCPP_BUILD_UBLKCTL=ON \
    -DUBLKCPP_BUILD_EXAMPLES=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Running the Examples

The examples are ublk block device daemons and need the ublk driver loaded:

```bash
sudo modprobe ublk_drv
sudo ./build/examples/ublk-nop -n 0
```

## Using ublkctl

`ublkctl` is a control tool for ublk devices. Run it with a subcommand:

```bash
sudo ./build/bin/ublkctl list    # list devices
sudo ./build/bin/ublkctl add -q 1 -d 32   # add a device
sudo ./build/bin/ublkctl del -n 0 # delete device 0
```

See `ublkctl --help` for all supported subcommands and options.
