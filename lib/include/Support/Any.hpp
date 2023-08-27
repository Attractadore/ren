#pragma once
#include "Optional.hpp"

#include <any>

namespace ren {

struct Any : std::any {
  using std::any::any;

  template <typename T> auto get() const noexcept -> Optional<const T &> {
    const T *value = std::any_cast<T>(this);
    if (value) {
      return *value;
    }
    return None;
  }

  template <typename T> auto get() noexcept -> Optional<T &> {
    T *value = std::any_cast<T>(this);
    if (value) {
      return *value;
    }
    return None;
  }
};

} // namespace ren
