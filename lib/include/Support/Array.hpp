#pragma once
#include <array>

template <typename T, typename... Us>
requires(std::convertible_to<Us, T> and...) constexpr auto makeArray(
    Us &&...args) {
  return std::array<T, sizeof...(Us)>{std::forward<Us>(args)...};
}
