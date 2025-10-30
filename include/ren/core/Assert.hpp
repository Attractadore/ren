#pragma once
#include "ren/core/StdDef.hpp"

#include <cstdlib>
#include <source_location>

namespace ren {

void assert_msg(std::source_location sl, const char *condition,
                const char *msg = nullptr);

void todo_msg(std::source_location sl, const char *msg);

} // namespace ren

#if REN_ASSERTIONS

#define ren_assert_msg(condition, msg)                                         \
  if (not(condition)) {                                                        \
    ::ren::assert_msg(std::source_location::current(), #condition, msg);       \
    ren_trap();                                                                \
    std::abort();                                                              \
  }

#define ren_assert(condition)                                                  \
  if (not(condition)) {                                                        \
    ::ren::assert_msg(std::source_location::current(), #condition);            \
    ren_trap();                                                                \
    std::abort();                                                              \
  }

#define ren_todo(reason)                                                       \
  do {                                                                         \
    ::ren::todo_msg(std::source_location::current(), reason);                  \
    ren_trap();                                                                \
    std::abort();                                                              \
  } while (0)
#else

#define ren_assert_msg(condition, msg) (void)(condition), (void)(msg)
#define ren_assert(condition) (void)(condition)
#define ren_todo(reason) (void)(reason)

#endif
