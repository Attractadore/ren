#pragma once
#include "ren/core/Assert.hpp"
#include "ren/core/StdDef.hpp"

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

inline u64 find_msb(u64 x) {
  ren_assert(x > 0);
  usize hi_bit = 63 - std::countl_zero(x);
  return hi_bit;
}

} // namespace ren
