#pragma once
#include <concepts>
#include <cstddef>
#include <tuple>

namespace ren {
namespace detail {
template <typename T, size_t Idx, typename... Ts>
constexpr size_t type_index_helper = [] {
  if constexpr (Idx >= sizeof...(Ts)) {
    return sizeof...(Ts);
  } else if constexpr (std::same_as<
                           T, std::tuple_element_t<Idx, std::tuple<Ts...>>>) {
    return Idx;
  } else {
    return type_index_helper<T, Idx + 1, Ts...>;
  }
}();

template <typename T, typename... Ts>
constexpr size_t type_index = type_index_helper<T, 0, Ts...>;
} // namespace detail

template <typename V, typename... Ks> class TypeMap {
  static constexpr size_t num_keys = sizeof...(Ks);
  std::array<V, num_keys> m_values = {};

private:
  template <typename K>
    requires(detail::type_index<K, Ks...> < sizeof...(Ks))
  static constexpr size_t key_index = detail::type_index<K, Ks...>;

public:
  using key_types = std::tuple<Ks...>;
  using value_type = V;

  template <typename K> constexpr value_type &get() {
    return m_values[key_index<K>];
  }

  template <typename K> constexpr const value_type &get() const {
    return m_values[key_index<K>];
  }

  template <typename K> constexpr void set(const value_type &value) {
    m_values[key_index<K>] = value;
  }

  template <typename K> constexpr void set(value_type &&value) noexcept {
    m_values[key_index<K>] = std::move(value);
  }
};
} // namespace ren
