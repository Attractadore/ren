#pragma once
#include <ranges>

namespace ren {

constexpr auto map = std::views::transform;

template <std::integral I> auto range(I begin, I end) {
  return std::views::iota(begin, end);
}

template <std::integral I> auto range(I end) { return range(I(0), end); }

} // namespace ren
