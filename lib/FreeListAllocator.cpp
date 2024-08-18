#include "FreeListAllocator.hpp"
#include "Support/Assert.hpp"

namespace ren {

auto FreeListAllocator::allocate() -> unsigned {
  if (!m_free_list.empty()) {
    auto idx = m_free_list.back();
    m_free_list.pop_back();
    return idx;
  }
  return m_top++;
}

void FreeListAllocator::free(unsigned idx) {
  ren_assert(idx);
  m_free_list.push_back(idx);
}

} // namespace ren
