#pragma once
#include "StdDef.hpp"
#include "TypeTraits.hpp"
#include "Vector.hpp"

#include <ranges>

namespace ren {

namespace detail {

template <typename K, typename V, typename Compare, typename KeyContainer,
          typename ValueContainer>
class LinearMapImpl {
public:
  auto size() const -> usize { return m_keys.size(); }

  bool empty() const { return size() == 0; }

  template <typename Self> auto begin(this Self &self) {
    return std::ranges::begin(std::views::zip(self.m_keys, self.m_values));
  }

  template <typename Self> auto end(this Self &self) {
    return std::ranges::end(std::views::zip(self.m_keys, self.m_values));
  }

  template <typename Self> auto find(this Self &self, const K &find_key) {
    return self.begin() +
           std::ranges::distance(
               self.m_keys.begin(),
               std::ranges::find_if(self.m_keys, [&](const K &key) {
                 return self.m_compare(key, find_key);
               }));
  }

  void insert(K key, V value) {
    ren_assert(find(key) == end());
    m_keys.push_back(key);
    m_values.push_back(value);
  }

  template <typename Self>
  auto operator[](this Self &self, const K &key) -> ConstLikeT<V, Self> & {
    return self.get(key);
  }

  template <typename Self>
  auto get(this Self &self, const K &key) -> ConstLikeT<V, Self> & {
    auto it = self.find(key);
    ren_assert(it != self.end());
    return std::get<1>(*it);
  }

  template <typename Self>
  auto try_get(this Self &self, const K &key) -> ConstLikeT<V, Self> * {
    auto it = self.find(key);
    if (it == self.end()) {
      return nullptr;
    };
    return &std::get<1>(*it);
  }

  void clear() {
    m_keys.clear();
    m_values.clear();
  }

private:
  KeyContainer m_keys;
  ValueContainer m_values;
  NO_UNIQUE_ADDRESS Compare m_compare;
};

} // namespace detail

template <typename K, typename V, class Compare = std::ranges::equal_to>
using LinearMap = detail::LinearMapImpl<K, V, Compare, Vector<K>, Vector<V>>;

template <typename K, typename V, usize N,
          class Compare = std::ranges::equal_to>
using SmallLinearMap =
    detail::LinearMapImpl<K, V, Compare, SmallVector<K, N>, SmallVector<V, N>>;

} // namespace ren
