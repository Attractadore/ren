#pragma once
#include "ren/core/Assert.hpp"

#include <ranges>

namespace ren {

inline constexpr auto map = std::views::transform;

template <std::integral I> auto range(I begin, I end) {
  ren_assert(begin <= end);
  return std::views::iota(begin, end);
}

template <std::integral I> auto range(I end) { return range(I(0), end); }

} // namespace ren
