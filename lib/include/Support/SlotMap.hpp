#pragma once
#include "Attractadore/SlotMap.hpp"
#include "Vector.hpp"

namespace ren {
namespace detail {
template <typename T> using DefaultVector = Vector<T>;
}

template <typename V, std::unsigned_integral K = uint32_t>
using SlotMap = Attractadore::SlotMap<V, K, detail::DefaultVector>;

template <typename V, size_t N = detail::BufferCount<V, 64>, std::unsigned_integral K = uint32_t>
using SmallSlotMap =
    Attractadore::SlotMap<V, K, SizedSmallVector<N>::template Vector>;
} // namespace ren
