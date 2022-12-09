#pragma once
#include "Attractadore/SlotMap.hpp"
#include "Vector.hpp"

namespace ren {
template <typename V, std::unsigned_integral K = uint32_t>
using SlotMap = Attractadore::SlotMap<V, K, Vector>;

template <typename V, size_t N, std::unsigned_integral K = uint32_t>
using SmallSlotMap =
    Attractadore::SlotMap<V, K, detail::SizedSmallVector<N>::template type>;
} // namespace ren
