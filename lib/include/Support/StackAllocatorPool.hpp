#pragma once
#include "StackAllocator.hpp"
#include "Vector.hpp"

namespace ren {
class StackAllocatorPool {
  SmallVector<StackAllocator, 8> m_allocators;

public:
  StackAllocatorPool(unsigned allocator_capacity) {
    m_allocators.emplace_back(allocator_capacity);
  }

  struct Allocation {
    unsigned idx;
    unsigned count;
  };

  std::pair<Allocation, unsigned> allocate(unsigned count, unsigned alignment) {
    if (count > get_allocator_capacity()) {
      unsigned idx = m_allocators.size();
      m_allocators.emplace_back(count);
      auto allocation = m_allocators.back().allocate(count, alignment);
      assert(allocation);
      return {{.idx = idx, .count = count}, *allocation};
    }
    for (unsigned i = 0; i < m_allocators.size(); ++i) {
      auto allocation = m_allocators[i].allocate(count, alignment);
      if (allocation) {
        return {{.idx = i, .count = count}, *allocation};
      }
    }
    unsigned idx = m_allocators.size();
    m_allocators.emplace_back(get_allocator_capacity());
    auto allocation = m_allocators.back().allocate(count, alignment);
    assert(allocation);
    return {{.idx = idx, .count = count}, *allocation};
  }

  void free(Allocation allocation) {
    auto idx = allocation.idx;
    assert(idx < m_allocators.size());
    m_allocators[idx].free(allocation.count);
  }

  unsigned get_allocator_capacity() const { return m_allocators[0].capacity(); }
};
} // namespace ren
