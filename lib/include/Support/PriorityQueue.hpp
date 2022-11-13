#pragma once
#include "Support/Vector.hpp"

#include <queue>

namespace ren {
namespace detail {
template <typename T, typename Comp>
using PriorityQueue = std::priority_queue<T, Vector<T>, Comp>;
}

template <typename T> using MaxQueue = detail::PriorityQueue<T, std::less<T>>;

template <typename T>
using MinQueue = detail::PriorityQueue<T, std::greater<T>>;
} // namespace ren
