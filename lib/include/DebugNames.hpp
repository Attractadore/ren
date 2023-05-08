#pragma once
#define REN_DEBUG_NAMES 1

#ifndef REN_DEBUG_NAMES
#define REN_DEBUG_NAMES 0
#endif

#if REN_DEBUG_NAMES

#include <fmt/format.h>

#include <cstdint>
#include <source_location>

#define REN_DEFAULT_DEBUG_NAME(Type)                                           \
  [](std::source_location sl = std::source_location::current()) {              \
    return fmt::format(Type " created at {}:{}", sl.file_name(), sl.line());   \
  }()

#define REN_DEBUG_NAME_FIELD(Type)                                             \
  std::string debug_name = REN_DEFAULT_DEBUG_NAME(Type)
#define REN_SET_DEBUG_NAME(name) .debug_name = name

#else

#define REN_DEFAULT_DEBUG_NAME(Type) ""
#define REN_DEBUG_NAME_FIELD(Type)
#define REN_SET_DEBUG_NAME(name)
#define REN_SET_NUMBERED_DEBUG_NAME(name)

#endif
