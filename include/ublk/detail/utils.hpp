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

} // namespace detail
} // namespace ublk