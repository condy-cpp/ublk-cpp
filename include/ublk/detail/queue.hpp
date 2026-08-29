/**
 * @file queue.hpp
 * @brief I/O queue implementations.
 */

#pragma once

#include "ublk/detail/task.hpp"
#include "ublk/detail/utils.hpp"
#include "ublk/handler.hpp"
#include "ublk/raw.hpp"
#include "ublk/ublk_cmd.h"
#include <atomic>
#include <cerrno>
#include <condy.hpp>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <sys/mman.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace ublk {
namespace detail {

namespace ex = condy::detail::ex;

inline bool need_alloc_buf(uint64_t flags) noexcept {
    return !(flags & (UBLK_F_SUPPORT_ZERO_COPY | UBLK_F_USER_COPY));
}

inline bool need_io_buf(uint64_t flags, uint32_t op_flags) noexcept {
    bool zero_copy = flags & UBLK_F_SUPPORT_ZERO_COPY;
    bool auto_reg = flags & UBLK_F_AUTO_BUF_REG;
    bool need_reg_buf = op_flags & UBLK_IO_F_NEED_REG_BUF;
    return need_reg_buf || (zero_copy && !auto_reg);
}

inline off_t io_desc_offset(size_t q_id) noexcept {
    return UBLKSRV_CMD_BUF_OFFSET +
           q_id * UBLK_MAX_QUEUE_DEPTH * sizeof(ublksrv_io_desc);
}

template <typename T>
concept has_res = requires(T t) {
    { t.res } -> std::convertible_to<int32_t>;
};

template <typename T>
concept has_zone_lba = requires(T t) {
    { t.zone_lba } -> std::convertible_to<uint64_t>;
};

inline void extract_io_result(auto &&r, int32_t &res,
                              uint64_t &zone_lba) noexcept {
    using R = std::decay_t<decltype(r)>;
    if constexpr (std::convertible_to<R, int32_t>) {
        res = r;
        zone_lba = 0;
    } else {
        static_assert(has_res<R>,
                      "returned result is neither int32_t nor has a "
                      "res field");
        res = r.res;
        zone_lba = 0;
        if constexpr (has_zone_lba<R>) {
            zone_lba = r.zone_lba;
        }
    }
}

inline std::error_code normalize_error(const auto &err, int v) noexcept {
    using E = std::decay_t<decltype(err)>;
    if constexpr (std::same_as<E, std::error_code>) {
        return err;
    } else if constexpr (std::same_as<E, std::exception_ptr>) {
        try {
            std::rethrow_exception(err);
        } catch (const std::system_error &se) {
            return se.code();
        } catch (...) {
            return std::error_code(v, std::generic_category());
        }
    } else {
        return std::error_code(v, std::generic_category());
    }
}

template <typename Sender>
inline auto wrap_handle_io(Sender &&sender, int32_t &res,
                           uint64_t &zone_lba) noexcept {
    return std::forward<Sender>(sender) |
           ex::stopped_as_error(
               std::error_code(ECANCELED, std::generic_category())) |
           ex::upon_error([](const auto &err) noexcept {
               return normalize_error(err, EIO);
           }) |
           ex::then([&](auto r) noexcept {
               using R = decltype(r);
               if constexpr (std::same_as<R, std::error_code>) {
                   res = -r.value();
                   zone_lba = 0;
               } else {
                   extract_io_result(r, res, zone_lba);
               }
               return std::make_tuple(res, zone_lba);
           });
}

template <typename Fd, IoHandler Handler, typename Alloc> class IoQueue {
public:
    IoQueue(Fd ublkc_fd, int ublkc_fd_raw, uint16_t q_id, uint64_t flags,
            uint16_t queue_depth, uint32_t max_io_buf_bytes, Handler &handler,
            const Alloc &alloc)
        : ublkc_fd_(ublkc_fd), q_id_(q_id), flags_(flags),
          queue_depth_(queue_depth), max_io_buf_bytes_(max_io_buf_bytes),
          handler_(handler), alloc_(alloc) {
        bool ok = false;
        auto d = defer([&] noexcept {
            if (!ok) {
                cleanup_();
            }
        });

        size_t size = queue_depth_ * sizeof(ublksrv_io_desc);
        auto off = io_desc_offset(q_id_);
        void *addr = mmap(0, size, PROT_READ, MAP_SHARED | MAP_POPULATE,
                          ublkc_fd_raw, off);
        if (addr == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(),
                                    "mmap io_desc");
        }
        iod_base_ = reinterpret_cast<ublksrv_io_desc *>(addr);

        const size_t page = sysconf(_SC_PAGESIZE);
        if (need_alloc_buf(flags_)) {
            buf_bases_.reserve(queue_depth_);
            for (uint16_t tag = 0; tag < queue_depth_; tag++) {
                void *buf = alloc_aligned(alloc_, max_io_buf_bytes_, page);
                buf_bases_.push_back(buf);
            }
        }

        ok = true;
    }

    IoQueue(const IoQueue &) = delete;
    IoQueue &operator=(const IoQueue &) = delete;
    IoQueue(IoQueue &&) = delete;
    IoQueue &operator=(IoQueue &&) = delete;

    ~IoQueue() { cleanup_(); }

public:
    template <typename Sched> ex::task<void, TaskEnv<Sched, Alloc>> run() {
        if constexpr (QueueHandler<Handler>) {
            co_await handler_.init_queue(q_id_);
        }

        auto d = defer([&]() noexcept {
            if constexpr (QueueHandler<Handler>) {
                handler_.destroy_queue(q_id_);
            }
        });

        ex::simple_counting_scope scope;
        AllocVector<std::exception_ptr, decltype(alloc_)> errs(queue_depth_,
                                                               alloc_);
        auto sched = co_await ex::read_env(ex::get_start_scheduler);
        for (uint16_t tag = 0; tag < queue_depth_; tag++) {
            auto task = worker_<Sched>(tag, iod_base_ + tag, get_buf_(tag));
            auto s =
                ex::starts_on(sched, std::move(task)) |
                ex::upon_error([&, tag](const std::exception_ptr &ep) noexcept {
                    errs[tag] = ep;
                });
            ex::spawn(std::move(s), scope.get_token());
        }

        co_await scope.join();

        for (auto &ep : errs) {
            if (ep) {
                std::rethrow_exception(ep);
            }
        }
    }

private:
    void cleanup_() noexcept {
        if (!buf_bases_.empty()) {
            const size_t page = sysconf(_SC_PAGESIZE);
            for (auto *buf : buf_bases_) {
                free_aligned(alloc_, buf, max_io_buf_bytes_, page);
            }
            buf_bases_.clear();
        }
        if (iod_base_) {
            size_t size = queue_depth_ * sizeof(ublksrv_io_desc);
            munmap(iod_base_, size);
            iod_base_ = nullptr;
        }
    }

    void *get_buf_(uint16_t tag) const noexcept {
        if (buf_bases_.empty()) {
            return nullptr;
        }
        return buf_bases_[tag];
    }

    template <typename Sched>
    ex::task<void, TaskEnv<Sched, Alloc>>
    worker_(uint16_t tag, const ublksrv_io_desc *iod, void *buf) {
        int32_t r;
        bool zero_copy = flags_ & UBLK_F_SUPPORT_ZERO_COPY;
        bool auto_reg = flags_ & UBLK_F_AUTO_BUF_REG;

        uint64_t buf_addr = reinterpret_cast<uint64_t>(buf);
        uint64_t sqe_addr = 0;
        if (auto_reg) {
            ublk_auto_buf_reg reg = {};
            reg.index = tag;
            reg.flags = UBLK_AUTO_BUF_REG_FALLBACK;
            sqe_addr = ublk_auto_buf_reg_to_sqe_addr(&reg);
        }

        r = co_await (
            raw::fetch_req(ublkc_fd_, q_id_, tag, buf_addr, sqe_addr) |
            ex::upon_error(
                [&](std::error_code ec) noexcept { return -ec.value(); }));
        if (r == UBLK_IO_RES_NEED_GET_DATA) {
            r = co_await (raw::need_get_data(ublkc_fd_, q_id_, tag, buf_addr) |
                          ex::upon_error([&](std::error_code ec) noexcept {
                              return -ec.value();
                          }));
        }
        if (r == UBLK_IO_RES_ABORT) {
            co_return;
        } else if (r < 0) {
            throw std::system_error(-r, std::generic_category(), "fetch_req");
        }

        while (true) {
            bool io_buf = need_io_buf(flags_, iod->op_flags);
            if (io_buf) {
                co_await raw::register_io_buf(ublkc_fd_, q_id_, tag, tag);
            }

            int32_t res;
            uint64_t zone_lba;
            const IoData io_data{q_id_, tag, iod, buf};
            co_await wrap_handle_io(handler_.handle_io(io_data), res, zone_lba);

            if (io_buf) {
                co_await raw::unregister_io_buf(ublkc_fd_, q_id_, tag, tag);
            }

            uint64_t curr_buf_addr = buf_addr;
            if (ublksrv_get_op(iod) == UBLK_IO_OP_ZONE_APPEND) {
                assert(buf_addr == 0);
                curr_buf_addr = zone_lba;
            }
            r = co_await (raw::commit_and_fetch_req(ublkc_fd_, q_id_, tag, res,
                                                    curr_buf_addr, sqe_addr) |
                          ex::upon_error([&](std::error_code ec) noexcept {
                              return -ec.value();
                          }));
            if (r == UBLK_IO_RES_NEED_GET_DATA) {
                r = co_await (
                    raw::need_get_data(ublkc_fd_, q_id_, tag, buf_addr) |
                    ex::upon_error([&](std::error_code ec) noexcept {
                        return -ec.value();
                    }));
            }
            if (r == UBLK_IO_RES_ABORT) {
                co_return;
            } else if (r < 0) {
                throw std::system_error(-r, std::generic_category(),
                                        "commit_and_fetch_req");
            }
        }
    }

private:
    Fd ublkc_fd_;
    uint16_t q_id_;
    uint64_t flags_;
    uint16_t queue_depth_;
    uint32_t max_io_buf_bytes_;
    Handler &handler_;
    Alloc alloc_;
    ublksrv_io_desc *iod_base_ = nullptr;
    AllocVector<void *, Alloc> buf_bases_;
};

template <typename Fd, IoHandler Handler, typename Alloc> class BatchIoQueue {
public:
    BatchIoQueue(Fd ublkc_fd, int ublkc_fd_raw, uint16_t q_id, uint64_t flags,
                 uint16_t queue_depth, uint32_t max_io_buf_bytes,
                 Handler &handler, const Alloc &alloc)
        : ublkc_fd_(ublkc_fd), q_id_(q_id), flags_(flags),
          queue_depth_(queue_depth), max_io_buf_bytes_(max_io_buf_bytes),
          handler_(handler), alloc_(alloc) {
        bool ok = false;
        auto d = defer([&] noexcept {
            if (!ok) {
                cleanup_();
            }
        });

        {
            size_t size = queue_depth_ * sizeof(ublksrv_io_desc);
            auto off = io_desc_offset(q_id_);
            void *addr = mmap(0, size, PROT_READ, MAP_SHARED | MAP_POPULATE,
                              ublkc_fd_raw, off);
            if (addr == MAP_FAILED) {
                throw std::system_error(errno, std::generic_category(),
                                        "mmap io_desc");
            }
            iod_base_ = reinterpret_cast<ublksrv_io_desc *>(addr);
        }

        const size_t page = sysconf(_SC_PAGESIZE);
        {
            size_t size = align_up(queue_depth_ * commit_element_size_(), page);
            commit_buf_size_ = size;
            commit_buf_[0] = alloc_aligned(alloc_, size * 2, page);
            commit_buf_[1] = static_cast<char *>(commit_buf_[0]) + size;
            if (mlock(commit_buf_[0], size * 2) < 0) {
                throw std::system_error(errno, std::generic_category(),
                                        "mlock commit_buf");
            }
        }

        {
            size_t size = align_up(queue_depth_ * sizeof(uint16_t), page);
            fetch_buf_size_ = size;
            fetch_buf_[0] = reinterpret_cast<uint16_t *>(
                alloc_aligned(alloc_, size * 2, page));
            fetch_buf_[1] = reinterpret_cast<uint16_t *>(
                reinterpret_cast<char *>(fetch_buf_[0]) + size);
            if (mlock(fetch_buf_[0], size * 2) < 0) {
                throw std::system_error(errno, std::generic_category(),
                                        "mlock fetch_buf");
            }
        }

        if (need_alloc_buf(flags_)) {
            buf_bases_.reserve(queue_depth_);
            for (uint16_t tag = 0; tag < queue_depth_; tag++) {
                void *buf = alloc_aligned(alloc_, max_io_buf_bytes_, page);
                buf_bases_.push_back(buf);
            }
        }

        ok = true;
    }

    BatchIoQueue(const BatchIoQueue &) = delete;
    BatchIoQueue &operator=(const BatchIoQueue &) = delete;
    BatchIoQueue(BatchIoQueue &&) = delete;
    BatchIoQueue &operator=(BatchIoQueue &&) = delete;

    ~BatchIoQueue() { cleanup_(); }

public:
    template <typename Sched> ex::task<void, TaskEnv<Sched, Alloc>> run() {
        auto sched = co_await ex::read_env(ex::get_scheduler);
        auto alloc = co_await ex::read_env(ex::get_allocator);

        if constexpr (QueueHandler<Handler>) {
            co_await handler_.init_queue(q_id_);
        }

        auto d = defer([&]() noexcept {
            if constexpr (QueueHandler<Handler>) {
                handler_.destroy_queue(q_id_);
            }
        });

        for (uint16_t tag = 0; tag < queue_depth_; tag++) {
            set_result_(0, tag, tag, 0);
        }
        co_await raw::prep_io_cmds(ublkc_fd_, q_id_, get_batch_flags_(),
                                   queue_depth_, commit_element_size_(),
                                   commit_buf_[0],
                                   queue_depth_ * commit_element_size_());

        FlusherState flusher;
        AllocVector<WorkerState, decltype(alloc)> workers(queue_depth_, alloc);

        ex::simple_counting_scope scope;
        std::exception_ptr flusher_err;
        AllocVector<std::exception_ptr, decltype(alloc)> worker_errs(
            queue_depth_, alloc);
        bool worker_stopped = false;
        size_t running_workers = queue_depth_;

        {
            auto task = flusher_<Sched>(flusher, running_workers);
            auto s = ex::starts_on(sched, std::move(task)) |
                     ex::upon_error([&](std::exception_ptr ep) noexcept {
                         flusher_err = std::move(ep);
                     });
            ex::spawn(std::move(s), scope.get_token());
        }
        for (uint16_t tag = 0; tag < queue_depth_; tag++) {
            auto task = worker_<Sched>(workers[tag], flusher, worker_stopped,
                                       tag, iod_base_ + tag, get_buf_(tag));
            auto s =
                ex::starts_on(sched, std::move(task)) |
                ex::upon_error([&, tag](const std::exception_ptr &ep) noexcept {
                    worker_errs[tag] = ep;
                }) |
                ex::then([&] noexcept {
                    if (--running_workers == 0) {
                        flusher.futex.notify_one();
                    }
                });
            ex::spawn(std::move(s), scope.get_token());
        }

        condy::ProvidedBufferQueue queue(2, IOU_PBUF_RING_INC);
        queue.push(condy::buffer(fetch_buf_[0], fetch_buf_size_));
        queue.push(condy::buffer(fetch_buf_[1], fetch_buf_size_));

        size_t off = 0;
        // NOLINTNEXTLINE(bugprone-exception-escape)
        auto fetch_cb = [&](std::pair<int32_t, condy::BufferInfo> r) noexcept {
            auto &[res, info] = r;
            auto bid = info.bid;
            auto *fetch_buf = fetch_buf_[bid];
            assert(res >= 0);
            size_t nr_tags = static_cast<size_t>(res) / sizeof(uint16_t);
            size_t end = off + nr_tags;
            for (size_t j = off; j < end; j++) {
                uint16_t tag = fetch_buf[j];
                auto &worker = workers[tag];
                write_once_(worker.flag, true);
                worker.futex.notify_one();
            }
            off = end;
            bool consumed = info.num_buffers;
            if (consumed) {
                auto r = queue.push(condy::buffer(fetch_buf, fetch_buf_size_));
                assert(r == bid);
                off = 0;
            }
        };

        int32_t res = 0;

        auto s =
            raw::fetch_io_cmds(ublkc_fd_, queue, q_id_, fetch_cb) |
            ex::then([](int32_t r, condy::BufferInfo) noexcept { return r; }) |
            ex::upon_error(
                [&](std::error_code ec) noexcept { return -ec.value(); }) |
            ex::then([&](int32_t r) noexcept { res = r; }) |
            ex::then([&]() noexcept {
                worker_stopped = true;
                for (auto &worker : workers) {
                    worker.futex.notify_one();
                }
            });

        co_await ex::when_all(std::move(s), scope.join());

        for (auto &ep : worker_errs) {
            if (ep) {
                std::rethrow_exception(ep);
            }
        }
        if (flusher_err) {
            std::rethrow_exception(flusher_err);
        }
        assert(res < 0);
        if (res != UBLK_IO_RES_ABORT) {
            throw std::system_error(-res, std::generic_category(),
                                    "fetch_io_cmds");
        }
    }

private:
    void cleanup_() noexcept {
        const size_t page = sysconf(_SC_PAGESIZE);
        if (!buf_bases_.empty()) {
            for (auto *buf : buf_bases_) {
                free_aligned(alloc_, buf, max_io_buf_bytes_, page);
            }
            buf_bases_.clear();
        }
        if (fetch_buf_[0]) {
            munlock(fetch_buf_[0], fetch_buf_size_ * 2);
            free_aligned(alloc_, fetch_buf_[0], fetch_buf_size_ * 2, page);
        }
        if (commit_buf_[0]) {
            munlock(commit_buf_[0], commit_buf_size_ * 2);
            free_aligned(alloc_, commit_buf_[0], commit_buf_size_ * 2, page);
        }
        if (iod_base_) {
            size_t size = queue_depth_ * sizeof(ublksrv_io_desc);
            munmap(iod_base_, size);
            iod_base_ = nullptr;
        }
    }

    template <typename T>
    static auto read_once_(const std::atomic<T> &a) noexcept {
        return a.load(std::memory_order_relaxed);
    }

    template <typename T, typename V>
    static void write_once_(std::atomic<T> &a, V v) noexcept {
        a.store(v, std::memory_order_relaxed);
    }

    bool need_f_buf_addr_() noexcept {
        return !(flags_ & UBLK_F_AUTO_BUF_REG) && need_alloc_buf(flags_);
    }

    bool need_f_zone_lba_() noexcept { return flags_ & UBLK_F_ZONED; }

    uint16_t get_batch_flags_() noexcept {
        uint16_t f = 0;
        if (flags_ & UBLK_F_AUTO_BUF_REG) {
            f |= UBLK_BATCH_F_AUTO_BUF_REG_FALLBACK;
        } else if (need_f_buf_addr_()) {
            f |= UBLK_BATCH_F_HAS_BUF_ADDR;
        }
        if (need_f_zone_lba_()) {
            f |= UBLK_BATCH_F_HAS_ZONE_LBA;
        }
        return f;
    }

    size_t commit_element_size_() noexcept {
        size_t size = sizeof(ublk_elem_header);
        if (need_f_buf_addr_()) {
            size += sizeof(uint64_t);
        }
        if (need_f_zone_lba_()) {
            size += sizeof(uint64_t);
        }
        return size;
    }

    ublk_elem_header &commit_f_hdr(size_t index, size_t pos) noexcept {
        return *reinterpret_cast<ublk_elem_header *>(
            static_cast<char *>(commit_buf_[index]) +
            pos * commit_element_size_());
    }

    uint64_t &commit_f_buf_addr(size_t index, size_t pos) noexcept {
        assert(need_f_buf_addr_());
        return *reinterpret_cast<uint64_t *>(
            reinterpret_cast<char *>(&commit_f_hdr(index, pos)) +
            sizeof(ublk_elem_header));
    }

    uint64_t &commit_f_zone_lba(size_t index, size_t pos) noexcept {
        assert(need_f_zone_lba_());
        size_t off = sizeof(ublk_elem_header);
        if (need_f_buf_addr_()) {
            off += sizeof(uint64_t);
        }
        return *reinterpret_cast<uint64_t *>(
            reinterpret_cast<char *>(&commit_f_hdr(index, pos)) + off);
    }

    void set_result_(size_t index, size_t pos, uint16_t tag,
                     int32_t res) noexcept {
        auto &hdr = commit_f_hdr(index, pos);
        hdr.tag = tag;
        hdr.result = res;
        if (flags_ & UBLK_F_AUTO_BUF_REG) {
            hdr.buf_index = tag;
        } else if (need_f_buf_addr_()) {
            commit_f_buf_addr(index, pos) =
                reinterpret_cast<uint64_t>(buf_bases_[tag]);
        }
    }

    void set_zone_lba_(size_t index, size_t pos, uint64_t lba) noexcept {
        assert(need_f_zone_lba_());
        commit_f_zone_lba(index, pos) = lba;
    }

    void *get_buf_(uint16_t tag) const noexcept {
        if (buf_bases_.empty()) {
            return nullptr;
        }
        return buf_bases_[tag];
    }

private:
    struct FlusherState {
        size_t index = 0;
        std::atomic<uint16_t> pending = 0;
        condy::Futex<uint16_t> futex{pending};
    };

    template <typename Sched>
    ex::task<void, TaskEnv<Sched, Alloc>> flusher_(FlusherState &state,
                                                   size_t &running_workers) {
        auto sched = co_await ex::read_env(ex::get_start_scheduler);
        while (true) {
            auto nr = read_once_(state.pending);
            if (nr > 0) {
                auto cur_index = state.index;
                state.index = 1 - state.index;
                write_once_(state.pending, 0);
                auto s = raw::commit_io_cmds(
                             ublkc_fd_, q_id_, get_batch_flags_(), nr,
                             commit_element_size_(), commit_buf_[cur_index],
                             nr * commit_element_size_()) |
                         ex::then([&](int32_t r) {
                             if (r != nr * commit_element_size_()) {
                                 throw std::runtime_error(
                                     "commit_io_cmds returned unexpected byte "
                                     "count");
                             }
                         });
                co_await std::move(s);
                co_await ex::schedule(sched);
            } else if (running_workers == 0) {
                co_return;
            } else {
                co_await state.futex.wait(0);
            }
        }
    }

    struct WorkerState {
        std::atomic<bool> flag = false;
        condy::Futex<bool> futex{flag};
    };

    template <typename Sched>
    ex::task<void, TaskEnv<Sched, Alloc>>
    worker_(WorkerState &state, FlusherState &flusher, bool &stopped,
            uint16_t tag, const ublksrv_io_desc *iod, void *buf) {
        while (true) {
            if (!stopped && !read_once_(state.flag)) {
                co_await state.futex.wait(false);
            }
            if (stopped) {
                co_return;
            }
            assert(read_once_(state.flag));
            write_once_(state.flag, false);

            bool io_buf = need_io_buf(flags_, iod->op_flags);
            if (io_buf) {
                co_await raw::register_io_buf(ublkc_fd_, q_id_, tag, tag);
            }

            int32_t res;
            uint64_t zone_lba;
            const IoData io_data{q_id_, tag, iod, buf};
            co_await wrap_handle_io(handler_.handle_io(io_data), res, zone_lba);

            if (io_buf) {
                co_await raw::unregister_io_buf(ublkc_fd_, q_id_, tag, tag);
            }

            auto cur_index = flusher.index;
            auto slot = read_once_(flusher.pending);
            set_result_(cur_index, slot, tag, res);
            if (ublksrv_get_op(iod) == UBLK_IO_OP_ZONE_APPEND) {
                set_zone_lba_(cur_index, slot, zone_lba);
            }
            write_once_(flusher.pending, slot + 1);
            if (slot == 0) {
                flusher.futex.notify_one();
            }
        }
    }

private:
    Fd ublkc_fd_;
    uint16_t q_id_;
    uint64_t flags_;
    uint16_t queue_depth_;
    uint32_t max_io_buf_bytes_;
    Handler &handler_;
    Alloc alloc_;
    ublksrv_io_desc *iod_base_ = nullptr;
    size_t commit_buf_size_ = 0;
    void *commit_buf_[2] = {};
    size_t fetch_buf_size_ = 0;
    uint16_t *fetch_buf_[2] = {};
    AllocVector<void *, Alloc> buf_bases_;
};

} // namespace detail
} // namespace ublk