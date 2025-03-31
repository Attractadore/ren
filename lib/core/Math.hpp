#pragma once
#include <bit>
#include <concepts>
#include <limits>

namespace ren {

constexpr auto ceil_div(std::unsigned_integral auto x,
                        std::unsigned_integral auto over) {
  return x / over + ((x % over) != 0);
}

constexpr auto pad(std::unsigned_integral auto x,
                   std::unsigned_integral auto multiple) {
  return ceil_div(x, multiple) * multiple;
}

template <std::unsigned_integral T> constexpr auto ilog2(T x) {
  return std::numeric_limits<T>::digits - std::countl_zero(x) - 1;
}

} // namespace ren
