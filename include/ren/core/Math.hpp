#pragma once
#include "Assert.hpp"
#include "StdDef.hpp"

#include <bit>

namespace ren {

inline u64 find_msb(u64 x) {
  ren_assert(x > 0);
  usize hi_bit = 63 - std::countl_zero(x);
  return hi_bit;
}

inline u64 next_po2(u64 x) {
  usize hi_bit = find_msb(x);
  return 1 << (hi_bit + 1);
}

} // namespace ren
