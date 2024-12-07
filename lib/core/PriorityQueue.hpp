#pragma once
#include "Vector.hpp"

#include <queue>

namespace ren {

template <typename T, typename Comp>
using PriorityQueue = std::priority_queue<T, Vector<T>, Comp>;

template <typename T> using MaxQueue = PriorityQueue<T, std::less<T>>;

template <typename T> using MinQueue = PriorityQueue<T, std::greater<T>>;

} // namespace ren
