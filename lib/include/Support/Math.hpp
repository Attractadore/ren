#pragma once
#include <concepts>

namespace ren {
auto ceilDiv(std::unsigned_integral auto x, std::unsigned_integral auto over) {
  return x / over + ((x % over) != 0);
}

auto pad(std::unsigned_integral auto x, std::unsigned_integral auto multiple) {
  return ceilDiv(x, multiple) * multiple;
}
} // namespace ren
