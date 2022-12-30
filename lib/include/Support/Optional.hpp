#pragma once
#include <optional>

namespace ren {
template <typename T> struct Optional : std::optional<T> {
  using std::optional<T>::optional;
  constexpr auto operator<=>(const Optional &) const = default;

  template <std::invocable<T &> F> void visit(F f) {
    if (this->has_value()) {
      f(this->value());
    }
  }

  template <std::invocable<const T &> F> void visit(F f) const {
    if (this->has_value()) {
      f(this->value());
    }
  }
};

inline constexpr auto None = std::nullopt;
} // namespace ren
