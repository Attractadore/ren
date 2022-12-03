#pragma once
#include <stdexcept>

namespace ren {
template <typename R> void throwIfFailed(R r, const char *msg) {
  if (r) {
    throw std::runtime_error{msg};
  }
}

inline void throwIfFailed(bool good, const char *msg) {
  if (!good) {
    throw std::runtime_error{msg};
  }
}

template <typename P> void throwIfFailed(P *ptr, const char *msg) {
  throwIfFailed(ptr != nullptr, msg);
}
} // namespace ren
