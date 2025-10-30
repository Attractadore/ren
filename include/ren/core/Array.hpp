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

  void push(NotNull<Arena *> arena, T value = {})
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

  void push(NotNull<Arena *> arena, const T *values, usize count) {
    [[unlikely]] if (m_size + count > m_capacity) {
      usize new_capacity = m_capacity > 0 ? 2 * m_capacity : 1;
      while (new_capacity < m_size + count) {
        new_capacity *= 2;
      }
      if (!m_data or !arena->expand(m_data, m_capacity, new_capacity)) {
        T *new_data =
            (T *)arena->allocate(new_capacity * sizeof(T), alignof(T));
        std::memcpy(new_data, m_data, m_capacity * sizeof(T));
        m_data = new_data;
      }
      m_capacity = new_capacity;
    }
    std::memcpy(&m_data[m_size], values, sizeof(T) * count);
    m_size += count;
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

  void clear() { m_size = 0; }

  T pop() {
    ren_assert(m_size > 0);
    return m_data[--m_size];
  }
};

template <typename T, usize S> struct StackArray {
  T m_data[S] = {};

public:
  using value_type = T;

  constexpr T &operator[](usize i) {
    ren_assert(i < S);
    return m_data[i];
  }

  constexpr const T &operator[](usize i) const {
    ren_assert(i < S);
    return m_data[i];
  }

  constexpr T *begin() { return m_data; }
  constexpr const T *begin() const { return m_data; }

  constexpr T *end() { return &m_data[S]; }
  constexpr const T *end() const { return &m_data[S]; }

  static constexpr usize size() { return S; }
};

} // namespace ren
