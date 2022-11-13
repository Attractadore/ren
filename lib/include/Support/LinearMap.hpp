#pragma once
#include "Vector.hpp"

#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>

namespace ren {
template <typename K, typename V, size_t InlineCapacity> class LinearMap {
  SmallVector<K, InlineCapacity> m_keys;
  SmallVector<V, InlineCapacity> m_values;

private:
  auto items() const { return ranges::views::zip(m_keys, m_values); }

public:
  using key_type = K;
  using value_type = V;

  auto keys() const { return items() | ranges::views::keys; }

  auto values() const { return items() | ranges::views::values; }

  auto begin() const { return items().begin(); }

  auto end() const { return items().end(); }

  auto find(const key_type &key) {
    return ranges::find(
        items(), key, [](const auto &kv) -> auto && { return kv.first; });
  }

  void insert(key_type key, value_type value) {
    m_keys.emplace_back(std::move(key));
    m_values.emplace_back(std::move(value));
  }
};
} // namespace ren
