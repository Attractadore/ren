#pragma once
#include "core/StdDef.hpp"
#include "core/String.hpp"

namespace ren {

#if REN_DEBUG_NAMES

using DebugName = String;
#define REN_DEBUG_NAME_FIELD(default_name) DebugName name = default_name

#else

using DebugName = DummyString;

#define REN_DEBUG_NAME_FIELD(default_name) NO_UNIQUE_ADDRESS DebugName name

#endif

} // namespace ren
