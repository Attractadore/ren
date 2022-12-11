#pragma once
#include <concepts>

namespace ren {
auto ceilDiv(std::unsigned_integral auto x, std::unsigned_integral auto over) {
  return x / over + ((x % over) != 0);
}
} // namespace ren
