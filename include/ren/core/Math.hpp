#pragma once
#include "Assert.hpp"
#include "StdDef.hpp"

namespace ren {

inline u64 find_lsb(u64 value) { return __builtin_ffsll(value) - 1; }

inline u64 find_msb(u64 x) {
  ren_assert(x > 0);
  return 63 - __builtin_clzll(x);
}

template <usize L> u64 find_aligned_ones_a(u64 value) {
  for (i32 s = 1; s < (i32)L; s *= 2) {
    value = value & (value >> s);
  }
  u64 mask = -1;
  if constexpr (L == 2) {
    mask = 0x5555555555555555;
  } else if constexpr (L == 4) {
    mask = 0x1111111111111111;
  } else if constexpr (L == 8) {
    mask = 0x0101010101010101;
  } else if constexpr (L == 16) {
    mask = 0x0001000100010001;
  } else {
    static_assert(L == 1);
  }
  return find_lsb(value & mask);
}

template <usize L> u64 find_aligned_ones_b(u64 value) {
  static_assert(L <= 32);
  u64 s = -1;
  for (i32 i = 64 - L; i >= 0; i -= L) {
    u64 mask = ((u64(1) << L) - 1) << i;
    s = (value & mask) == mask ? i : s;
  }
  return s;
}

template <usize L> u64 find_aligned_ones(u64 value) {
  static_assert((L & (L - 1)) == 0);
  if constexpr (L == 1 or L == 2 or L == 4 or L == 8 or L == 16) {
    return find_aligned_ones_a<L>(value);
  } else {
    return find_aligned_ones_b<L>(value);
  }
}

inline u64 next_po2(u64 x) { return u64(1) << (find_msb(x) + 1); }

} // namespace ren
