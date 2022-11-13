#pragma once
#include "Attractadore/SlotMap.hpp"
#include "Vector.hpp"

namespace ren {
namespace detail {
template <typename T> using DefaultVector = Vector<T>;
}

template <typename T>
using SlotMap = Attractadore::SlotMap<T, uint64_t, detail::DefaultVector>;

template <typename T, size_t N>
using SmallSlotMap =
    Attractadore::SlotMap<T, uint64_t, SizedSmallVector<N>::template impl>;
} // namespace ren
