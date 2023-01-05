#pragma once
#include <fmt/format.h>

#include <source_location>
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

[[noreturn]] inline void
todo(std::source_location sl = std::source_location::current()) {
  throw std::runtime_error(fmt::format("{}:{}: {} not implemented!",
                                       sl.file_name(), sl.line(),
                                       sl.function_name()));
}

} // namespace ren
