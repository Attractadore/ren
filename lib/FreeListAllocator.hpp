#pragma once
#include "Support/Vector.hpp"

namespace ren {

class FreeListAllocator {
  unsigned m_top = 1;
  Vector<unsigned> m_free_list;

public:
  auto allocate() -> unsigned;

  void free(unsigned idx);
};

} // namespace ren
