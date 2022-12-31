#pragma once
#include "Optional.hpp"
#include "Vector.hpp"

namespace ren {

class FreeListAllocator {
  unsigned m_capacity;
  unsigned m_num_allocated = 0;
  Vector<unsigned> m_free_list;

public:
  FreeListAllocator(unsigned capacity) : m_capacity(capacity) {}

  Optional<unsigned> allocate() {
    if (not m_free_list.empty()) {
      auto idx = m_free_list.back();
      m_free_list.pop_back();
      m_num_allocated++;
      return idx;
    }
    if (m_num_allocated == m_capacity) {
      return None;
    }
    return m_num_allocated++;
  }

  void free(unsigned idx) { m_free_list.push_back(idx); }

  void expand(unsigned new_capacity) {
    assert(new_capacity >= m_capacity);
    m_capacity = new_capacity;
  }
};

} // namespace ren
