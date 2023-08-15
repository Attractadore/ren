#pragma once
#include "Optional.hpp"

#include <any>

namespace ren {

struct Any : std::any {
  using std::any::any;

  template <typename T> auto get() const noexcept -> Optional<const T &> {
    return get<T>(const_cast<Any *>(this));
  }

  template <typename T> auto get() noexcept -> Optional<T &> {
    const T *value = std::any_cast<T>(this);
    if (value) {
      return *value;
    }
    return None;
  }
};

} // namespace ren
