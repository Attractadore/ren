#pragma once
#include "Support/String.hpp"

#ifndef REN_DEBUG_NAMES
#define REN_DEBUG_NAMES 0
#endif

namespace ren {

struct DummyString {
  DummyString() = default;
  DummyString(const char *) {}
  DummyString(const std::string &) {}
  DummyString(std::string_view) {}

  bool operator==(const DummyString&) const = default;
};

#if REN_DEBUG_NAMES

using DebugName = String;
#define REN_DEBUG_NAME_FIELD(default_name) DebugName name = default_name

#else

using DebugName = DummyString;

#define REN_DEBUG_NAME_FIELD(default_name) [[no_unique_address]] DebugName name

#endif

} // namespace ren
