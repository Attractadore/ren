#pragma once
#include "ren/core/StdDef.hpp"

#include <cstdio>
#include <cstdlib>
#include <fmt/base.h>
#include <source_location>
#include <utility>

namespace ren {

template <typename... Ts>
[[noreturn]] inline void unreachable(fmt::format_string<Ts...> fmt_str,
                                     Ts &&...args) {
  fmt::println(stderr, std::move(fmt_str), std::forward<Ts>(args)...);
  std::fputc('\n', stderr);
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
todo(const char *message,
     std::source_location sl = std::source_location::current()) {
  fmt::println(stderr, "{}:{}: {}", sl.file_name(), sl.line(), message);
  ren_trap();
  std::abort();
}

} // namespace ren
