#include "FreeListAllocator.hpp"
#include "ren/core/Assert.hpp"

namespace ren {

auto FreeListAllocator::allocate() -> unsigned {
  if (!m_free_list.empty()) {
    auto idx = m_free_list.back();
    m_free_list.pop_back();
    return idx;
  }
  return m_top++;
}

auto FreeListAllocator::allocate(u32 idx) -> unsigned {
  if (!m_free_list.empty()) {
    auto it = std::ranges::find(m_free_list, idx);
    if (it != m_free_list.end()) {
      std::swap(*it, m_free_list.back());
      m_free_list.pop_back();
      return idx;
    }
  }
  if (m_top > idx) {
    return 0;
  }
  while (m_top < idx) {
    m_free_list.push_back(m_top++);
  }
  return m_top++;
}

void FreeListAllocator::free(unsigned idx) {
  ren_assert(idx);
  m_free_list.push_back(idx);
}

} // namespace ren
