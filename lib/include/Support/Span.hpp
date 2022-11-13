#pragma once
#include <span>

namespace ren {
template <typename T> std::span<T> asSpan(T &value) { return {&value, 1}; }
} // namespace ren
