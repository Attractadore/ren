#pragma once
#include "Macros.hpp"
#include "String.hpp"

#include <fmt/format.h>
#include <source_location>
#include <stdexcept>
#include <utility>

namespace ren {

template <typename R> void throw_if_failed(R r, const char *msg) {
  [[unlikely]] if (r) { throw std::runtime_error{msg}; }
}

inline void throw_if_failed(bool good, const char *msg) {
  [[unlikely]] if (!good) { throw std::runtime_error{msg}; }
}

template <typename P> void throw_if_failed(P *ptr, const char *msg) {
  throw_if_failed(ptr != nullptr, msg);
}

template <typename... Ts>
[[noreturn]] inline void unreachable(fmt::format_string<Ts...> fmt_str,
                                     Ts &&...args) {
  fmt::print(stderr, "{}\n",
             fmt::format(std::move(fmt_str), std::forward<Ts>(args)...));
  ren_trap();
  std::abort();
}

[[noreturn]] inline void
todo(std::source_location sl = std::source_location::current()) {
  fmt::println(stderr, "{}:{}: {} not implemented!", sl.file_name(), sl.line(),
               sl.function_name());
  ren_trap();
  std::abort();
}

template <typename... Ts>
[[noreturn]] inline void
todo(StringView message,
     std::source_location sl = std::source_location::current()) {
  fmt::println(stderr, "{}:{}: {}", sl.file_name(), sl.line(), message);
  ren_trap();
  std::abort();
}

} // namespace ren
