#include "ren/core/Assert.hpp"
#include "ren/core/Math.hpp"

#include <cstdint>

int main() {
  using namespace ren;
  for (usize i : range<u64>(0, UINT16_MAX + 1)) {
    u64 a, b;
    a = find_aligned_ones_a<1>(i);
    b = find_aligned_ones_b<1>(i);
    ren_assert(a == b);
    a = find_aligned_ones_a<2>(i);
    b = find_aligned_ones_b<2>(i);
    ren_assert(a == b);
    a = find_aligned_ones_a<4>(i);
    b = find_aligned_ones_b<4>(i);
    ren_assert(a == b);
    a = find_aligned_ones_a<8>(i);
    b = find_aligned_ones_b<8>(i);
    ren_assert(a == b);
    a = find_aligned_ones_a<16>(i);
    b = find_aligned_ones_b<16>(i);
    ren_assert(a == b);
  }
}
