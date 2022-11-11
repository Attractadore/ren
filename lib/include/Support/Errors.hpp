#pragma once
#include <stdexcept>

namespace Ren {
template <typename R> void ThrowIfFailed(R r, const char *msg) {
  if (r) {
    throw std::runtime_error{msg};
  }
}

inline void ThrowIfFailed(bool good, const char *msg) {
  if (!good) {
    throw std::runtime_error{msg};
  }
}
} // namespace Ren
