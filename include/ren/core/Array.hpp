#pragma once
#include "Arena.hpp"
#include "NotNull.hpp"
#include "StdDef.hpp"

#include <cstring>

namespace ren {

struct Arena;

template <typename T> struct DynamicArray {
  T *m_data = nullptr;
  u32 m_size = 0;
  u32 m_capacity = 0;

public:
  [[nodiscard]] static DynamicArray init(NotNull<Arena *> arena,
                                         usize capacity) {
    DynamicArray array;
    array.reserve(arena, capacity);
    return array;
  }

  T *begin() { return m_data; }
  const T *begin() const { return m_data; }

  T *end() { return m_data + m_size; }
  const T *end() const { return m_data + m_size; }

  void push(NotNull<Arena *> arena, T value)
    requires std::is_trivially_destructible_v<T>
  {
    [[unlikely]] if (m_size + 1 > m_capacity) {
      usize new_capacity = m_capacity > 0 ? 2 * m_capacity : 1;
      if (!m_data or !arena->expand(m_data, m_capacity, new_capacity)) {
        T *new_data =
            (T *)arena->allocate(new_capacity * sizeof(T), alignof(T));
        std::memcpy(new_data, m_data, m_capacity * sizeof(T));
        m_data = new_data;
      }
      m_capacity = new_capacity;
    }
    m_data[m_size++] = value;
  }

  T &back() {
    ren_assert(m_size > 0);
    return m_data[m_size - 1];
  }

  const T &back() const {
    ren_assert(m_size > 0);
    return m_data[m_size - 1];
  }

  T &operator[](usize i) {
    ren_assert(i < m_size);
    return m_data[i];
  }

  const T &operator[](usize i) const {
    ren_assert(i < m_size);
    return m_data[i];
  }

  void reserve(NotNull<Arena *> arena, usize new_capacity) {
    if (new_capacity <= m_capacity) {
      return;
    }
    if (!m_data or !arena->expand(m_data, m_capacity, new_capacity)) {
      T *new_data = (T *)arena->allocate(new_capacity * sizeof(T), alignof(T));
      std::memcpy(new_data, m_data, m_capacity * sizeof(T));
      m_data = new_data;
    }
    m_capacity = new_capacity;
  }
};

} // namespace ren
