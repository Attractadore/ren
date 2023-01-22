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

template <typename... Ts>
[[noreturn]] inline void unreachable(fmt::format_string<Ts...> fmt_str,
                                     Ts &&...args) {
  fmt::print(stderr, "{}\n",
             fmt::format(std::move(fmt_str), std::forward<Ts>(args)...));
  abort();
}

[[noreturn]] inline void
todo(std::source_location sl = std::source_location::current()) {
  unreachable("{}:{}: {} not implemented!", sl.file_name(), sl.line(),
              sl.function_name());
}

template <typename... Ts>
[[noreturn]] inline void
todo(std::string_view message,
     std::source_location sl = std::source_location::current()) {
  unreachable("{}:{}: {}", sl.file_name(), sl.line(), message);
}

} // namespace ren
