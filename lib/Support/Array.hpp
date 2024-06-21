#pragma once
#include <array>

namespace ren {

template <typename T, typename... Us>
  requires(std::convertible_to<Us, T> and ...)
constexpr auto make_array(Us &&...args) {
  return std::array<T, sizeof...(Us)>{std::forward<Us>(args)...};
}

} // namespace ren
