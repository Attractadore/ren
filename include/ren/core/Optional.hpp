#pragma once
#include "Assert.hpp"

namespace ren {

template <typename T> struct Optional {
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
};

} // namespace ren
