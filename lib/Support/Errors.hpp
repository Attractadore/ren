#pragma once
#include <boost/predef/compiler.h>
#include <cassert>
#include <fmt/format.h>
#include <source_location>
#include <stdexcept>

namespace ren {

#if BOOST_COMP_GNUC || BOOST_COMP_CLANG
#define ren_trap __builtin_trap
#else
#define ren_trap abort
#endif

template <typename R> void throw_if_failed(R r, const char *msg) {
  if (r) {
    throw std::runtime_error{msg};
  }
}

inline void throw_if_failed(bool good, const char *msg) {
  if (!good) {
    throw std::runtime_error{msg};
  }
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

#ifndef REN_ASSERTIONS
#define REN_ASSERTIONS 0
#endif

#if REN_ASSERTIONS
#define ren_assert_msg(condition, msg)                                         \
  if (not(condition)) {                                                        \
    auto sl = std::source_location::current();                                 \
    fmt::println(stderr, "{}:{}: {}: Assertion \"{}\" failed: {}",             \
                 sl.file_name(), sl.line(), sl.function_name(), #condition,    \
                 msg);                                                         \
    ren_trap();                                                                \
  }
#define ren_assert(condition)                                                  \
  if (not(condition)) {                                                        \
    auto sl = std::source_location::current();                                 \
    fmt::println(stderr, "{}:{}: {}: Assertion \"{}\" failed", sl.file_name(), \
                 sl.line(), sl.function_name(), #condition);                   \
    ren_trap();                                                                \
  }
#else
#define ren_assert_msg(condition, msg) (void)(condition), (void)(msg)
#define ren_assert(condition) (void)(condition)
#endif

} // namespace ren
