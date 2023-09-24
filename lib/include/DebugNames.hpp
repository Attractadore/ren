#pragma once
#include "Support/String.hpp"

#ifndef REN_DEBUG_NAMES
#define REN_DEBUG_NAMES 0
#endif

namespace ren {

#if REN_DEBUG_NAMES

using DebugName = String;

#define REN_DEBUG_NAME_FIELD(default_name) DebugName name = default_name

#else

struct DebugName {
  DebugName() = default;
  DebugName(const char *) {}
  DebugName(const std::string &) {}
  DebugName(std::string_view) {}
};

#define REN_DEBUG_NAME_FIELD(default_name) [[no_unique_address]] DebugName name

#endif

} // namespace ren
