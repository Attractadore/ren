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
  m_frame_freed[m_frame_index].push_back(idx);
}

void FreeListAllocator::next_frame() {
  m_frame_index = (m_frame_index + 1) % PIPELINE_DEPTH;
  m_free_list.append(m_frame_freed[m_frame_index]);
  m_frame_freed[m_frame_index].clear();
}

} // namespace ren
