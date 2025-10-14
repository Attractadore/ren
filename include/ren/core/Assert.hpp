#pragma once
#if REN_ASSERTIONS
#include "ren/core/StdDef.hpp"

#include <fmt/base.h>
#include <source_location>

#define ren_assert_msg(condition, msg)                                         \
  [[unlikely]] if (not(condition)) {                                           \
    auto sl = std::source_location::current();                                 \
    fmt::println(stderr, "{}:{}: {}: Assertion \"{}\" failed: {}",             \
                 sl.file_name(), sl.line(), sl.function_name(), #condition,    \
                 msg);                                                         \
    ren_trap();                                                                \
  }

#define ren_assert(condition)                                                  \
  [[unlikely]] if (not(condition)) {                                           \
    auto sl = std::source_location::current();                                 \
    fmt::println(stderr, "{}:{}: {}: Assertion \"{}\" failed", sl.file_name(), \
                 sl.line(), sl.function_name(), #condition);                   \
    ren_trap();                                                                \
  }
#else

#define ren_assert_msg(condition, msg) (void)(condition), (void)(msg)
#define ren_assert(condition) (void)(condition)

#endif
