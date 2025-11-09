#pragma once
#include "Assert.hpp"

#include <type_traits>

namespace ren {

template <typename T>
  requires std::is_trivially_destructible_v<T>
struct Optional {
  T m_value = T();
  bool m_active = false;

public:
  Optional() = default;

  Optional(T value) {
    m_value = value;
    m_active = true;
  }

  explicit operator bool() const { return m_active; }

  T &operator*() {
    ren_assert(m_active);
    return m_value;
  }

  const T &operator*() const {
    ren_assert(m_active);
    return m_value;
  }

  T *operator->() {
    ren_assert(m_active);
    return &m_value;
  }

  const T *operator->() const {
    ren_assert(m_active);
    return &m_value;
  }
};

} // namespace ren
