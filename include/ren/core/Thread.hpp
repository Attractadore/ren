#pragma once
#include "Span.hpp"
#include "StdDef.hpp"

namespace ren {

struct Processor {
  u32 id = 0;
  u32 core_id = 0;
  u32 die_id = 0;
  u32 package_id = 0;
  u32 cluster_id = 0;
};

Span<Processor> cpu_topology(NotNull<Arena *> arena);

} // namespace ren
