/**
 * @file utils.hpp
 * @brief Small utility helpers.
 */

#pragma once

#include <utility>
#include <vector>

namespace ublk {
namespace detail {

template <typename Func> auto defer(Func &&func) noexcept {
    using F = std::decay_t<Func>;
    class [[nodiscard]] Defer {
    public:
        Defer(F func) : func_(std::move(func)) {}
        ~Defer() { func_(); }

        Defer(const Defer &) = delete;
        Defer &operator=(const Defer &) = delete;
        Defer(Defer &&) = delete;
        Defer &operator=(Defer &&) = delete;

    private:
        F func_;
    };
    return Defer(std::forward<Func>(func));
}

template <typename T, typename Alloc>
using AllocVector = std::vector<
    T, typename std::allocator_traits<Alloc>::template rebind_alloc<T>>;

template <typename T> inline T align_up(T value, T alignment) noexcept {
    // alignment must be a power of two
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0);
    return (value + alignment - 1) & ~(alignment - 1);
}

template <typename T>
concept is_pmr_allocator = requires(const T &a) {
    { a.resource() } -> std::convertible_to<std::pmr::memory_resource *>;
};

template <typename Alloc>
[[nodiscard]] inline void *alloc_aligned(const Alloc &alloc, size_t bytes,
                                         size_t alignment) {
    if constexpr (is_pmr_allocator<Alloc>) {
        return alloc.resource()->allocate(bytes, alignment);
    } else {
        return ::operator new(bytes, std::align_val_t{alignment});
    }
}

template <typename Alloc>
inline void free_aligned(const Alloc &alloc, void *ptr, size_t bytes,
                         size_t alignment) noexcept {
    if constexpr (is_pmr_allocator<Alloc>) {
        alloc.resource()->deallocate(ptr, bytes, alignment);
    } else {
        ::operator delete(ptr, bytes, std::align_val_t{alignment});
    }
}

} // namespace detail
} // namespace ublk