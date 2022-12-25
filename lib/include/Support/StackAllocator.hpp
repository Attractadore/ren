#pragma once
#include "Math.hpp"
#include "Optional.hpp"

namespace ren {
class StackAllocator {
  unsigned m_capacity;
  unsigned m_num_allocated = 0;
  unsigned m_num_freed = 0;

public:
  StackAllocator(unsigned capacity) : m_capacity(capacity) {}

  Optional<unsigned> allocate(unsigned count, unsigned alignment) {
    unsigned start = pad(m_num_allocated, alignment);
    unsigned num_allocated = start + count;
    if (num_allocated > m_capacity) {
      return None;
    }
    m_num_allocated = num_allocated;
    unsigned pad_count = start - m_num_allocated;
    m_num_freed += pad_count;
    return start;
  }

  void free(unsigned count) {
    unsigned num_freed = m_num_freed + count;
    if (num_freed == m_num_allocated) {
      m_num_allocated = 0;
      m_num_freed = 0;
    } else {
      m_num_freed = num_freed;
    }
  }

  unsigned capacity() const { return m_capacity; }
  unsigned used_capacity() const { return m_num_allocated; }
  unsigned free_capacity() const { return capacity() - used_capacity(); }
};
} // namespace ren
