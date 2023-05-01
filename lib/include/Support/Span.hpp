#pragma once
#include <cassert>
#include <span>

namespace ren {

template <typename T> std::span<const T> asSpan(const T &value) {
  return {&value, 1};
}

template <typename T> std::span<T> asSpan(T &value) { return {&value, 1}; }

template <typename T, typename U>
std::span<T> reinterpret_span(std::span<U> from) {
  auto to = std::span(reinterpret_cast<T *>(from.data()),
                      from.size_bytes() / sizeof(T));
  assert(to.size_bytes() == from.size_bytes());
  return to;
}

} // namespace ren
