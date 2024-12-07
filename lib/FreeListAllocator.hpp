#pragma once
#include "core/StdDef.hpp"
#include "core/Vector.hpp"

namespace ren {

class FreeListAllocator {
  u32 m_top = 1;
  Vector<u32> m_free_list;

public:
  auto allocate() -> u32;

  auto allocate(u32 idx) -> u32;

  void free(u32 idx);
};

} // namespace ren
