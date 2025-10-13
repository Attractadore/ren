#pragma once
#include "Arena.hpp"
#include "NotNull.hpp"
#include "StdDef.hpp"

namespace ren {

struct Arena;

template <typename T>
  requires std::is_trivially_destructible_v<T>
struct DynamicArray {
  T *m_data = nullptr;
  usize m_size = 0;
  usize m_capacity = 0;

public:
  T *begin() { return m_data; }
  const T *begin() const { return m_data; }

  T *end() { return m_data + m_size; }
  const T *end() const { return m_data + m_size; }

  void push(NotNull<Arena *> arena, T value) {
    [[unlikely]] if (m_size + 1 > m_capacity) {
      usize new_capacity = m_capacity > 0 ? 2 * m_capacity : 1;
      if (!m_data or !ren::expand<T>(arena, m_data, m_capacity, new_capacity)) {
        T *new_data =
            (T *)allocate(arena, new_capacity * sizeof(T), alignof(T));
        std::memcpy(new_data, m_data, m_capacity * sizeof(T));
        m_data = new_data;
      }
      m_capacity = new_capacity;
    }
    m_data[m_size++] = value;
  }
};

} // namespace ren
