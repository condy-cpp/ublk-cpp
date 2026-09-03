# ublk-cpp

![C++](https://img.shields.io/badge/C++-26-blue)
![License](https://img.shields.io/github/license/condy-cpp/ublk-cpp)
![Release](https://img.shields.io/github/v/release/condy-cpp/ublk-cpp)
![Stars](https://img.shields.io/github/stars/condy-cpp/ublk-cpp?style=social)

![CI (Main)](https://github.com/condy-cpp/ublk-cpp/actions/workflows/ci-main.yml/badge.svg?branch=master)
![CI (Latest Kernel)](https://github.com/condy-cpp/ublk-cpp/actions/workflows/ci-latest-kernel.yml/badge.svg?branch=master)
![CI (Static Check)](https://github.com/condy-cpp/ublk-cpp/actions/workflows/ci-static-check.yml/badge.svg?branch=master)
![Deploy Docs](https://github.com/condy-cpp/ublk-cpp/actions/workflows/deploy-docs.yml/badge.svg?branch=master)

ublk-cpp is an intuitive, highly extensible C++ library for writing [ublk servers](https://docs.kernel.org/block/ublk.html):

- **Comprehensive ublk Support**
  Full coverage of the ublk userspace interface — device lifecycle management, per-queue I/O loops, and advanced features such as user recovery and shm buffer registration.

- **Full io_uring Ecosystem**
  Built on top of [Condy](https://github.com/condy-cpp/condy), all kinds of io_uring async operations — `send`/`recv`, `read`/`write`, and more — can be used directly and intuitively inside your ublk server.

- **C++26 Sender Model**
  The API is built on `std::execution` senders — handlers are ordinary senders that compose with standard algorithms and can interoperate with any asynchronous driver.

> [!NOTE]
> This repository is experimental and will not reach a stable state until
> `std::execution` (C++26) is finalized.

## Documentation

- **[Online Docs (GitHub Pages)](https://condy-cpp.github.io/ublk-cpp/)**
- **[User Guide](docs/guide.md):** Step-by-step introduction to ublk-cpp's concepts and usage.
- **[Building and Usage](docs/build.md):** How to build and integrate ublk-cpp in your project.
- **[Examples](docs/examples.md):** Practical ublk-cpp code samples.

## Support

- For questions, bug reports, or feature requests, please open an [issue](https://github.com/condy-cpp/ublk-cpp/issues).
- [Pull requests](https://github.com/condy-cpp/ublk-cpp/pulls) are welcome!
