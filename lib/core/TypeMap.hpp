#pragma once
#include "StdDef.hpp"
#include "TypeTraits.hpp"

#include <array>
#include <concepts>
#include <utility>

namespace ren {

namespace detail {

template <typename K, usize I, typename T, typename... Ts>
constexpr usize KEY_INDEX_IMPL = [] {
  if constexpr (std::same_as<K, T>) {
    return I;
  } else {
    return KEY_INDEX_IMPL<K, I + 1, Ts...>;
  }
}();

} // namespace detail

template <typename V, typename... Ks> class TypeMap {
public:
  template <typename K, typename Self>
  auto get(this Self &self) -> ConstLikeT<V, Self> & {
    return self.m_values[KEY_INDEX<K>];
  }

  template <typename K, typename U> void set(U &&value) {
    m_values[KEY_INDEX<K>] = std::forward<U>(value);
  }

private:
  template <typename K>
    requires(std::same_as<K, Ks> or ...)
  static constexpr usize KEY_INDEX = detail::KEY_INDEX_IMPL<K, 0, Ks...>;

private:
  std::array<V, sizeof...(Ks)> m_values = {};
};

} // namespace ren
